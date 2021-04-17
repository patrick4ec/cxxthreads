//===------------------- cxxthreads/threadpool.h ----------------*- C++ -*-===//
//
//  Under Apache License v2.0.
//  Author: Patrick
//
//===----------------------------------------------------------------------===//
//
// This file defines the ThreadPool class. 
//
//===----------------------------------------------------------------------===//

#ifndef CXXTHREADS_THREADPOOL_H
#define CXXTHREADS_THREADPOOL_H

#include "blocking_queue.h"
#include "null_pointer_exception.h"
#include "rejected_execution_exception.h"
#include "task.h"
#include "util.h"

#include <gsl/gsl_util>

#include <boost/thread.hpp>

#include <atomic>
#include <memory>
#include <type_traits>
#include <unordered_map>



namespace cxxthreads 
{

  namespace detail
  {
    template <typename T>
    struct list
    {
      struct Node
      {
        std::shared_ptr<T> data_;
        std::shared_ptr<Node> next_;
      };

      list() : head_(new Node)
      {
        tail_ = head_;
      }

      void pushTail(std::shared_ptr<Node> new_node) noexcept
      {
        tail_->next_ = std::move(new_node);
        tail_ = tail_->next_;
      }

      std::shared_ptr<Node> popHead() noexcept
      {
        if(empty())
          return nullptr;
        
        auto node = head_->next_;
        if(head_->next_ == tail_)
          tail_ = head_;
        head_->next_ = head_->next_->next_;
        return node;
      }

      bool empty() const noexcept
      {
        return head_ == tail_;
      }

      std::shared_ptr<Node> head_;
      std::shared_ptr<Node> tail_;
    };
  }; // namespace detail



/// A dynamic thread pool implementation.
///
/// \headerfile "cxxthreads/threadpool.h"
template<typename BlockingQueue>
class ThreadPool 
{
public:
  /// Type of rejected execution handler.
  using rejected_execution_handler_type = std::function<void(std::shared_ptr<Task> task, ThreadPool &pool)>;

  /// A handler for rejected tasks that throws a RejectedExecutionException.
  struct AbortPolicy
  {
    ///  Always throws RejectedExecutionException.
    ///
    ///  \param task The runnable task requested to be executed.
    ///  \param pool The pool attempting to execute this task.
    ///  \throws RejectedExecutionException always
    void operator()(std::shared_ptr<Task> task, ThreadPool &pool)
    {
      throw RejectedExecutionException("Task rejected");
    }
  };

  /// A handler for rejected tasks that silently discards the
  /// rejected task.
  struct DiscardPolicy
  {
    /// Does nothing, which has the effect of discarding task.
    ///
    /// \param task The runnable task requested to be executed.
    /// \param pool The executor attempting to execute this task.
    void operator()(std::shared_ptr<Task> task, ThreadPool &pool)
    {}
  };

  /// A handler for rejected tasks that runs the rejected task
  /// directly in the calling thread of the ThreadPool::execute method,
  /// unless the executor has been shut down, in which case the task
  /// is discarded.
  struct CallerRunPolicy
  {
    /// Executes task in the caller's thread, unless the executor
    /// has been shut down, in which case the task is discarded.
    ///
    /// \param task The runnable task requested to be executed.
    /// \param pool The executor attempting to execute this task.
    void operator()(std::shared_ptr<Task> task, ThreadPool &pool)
    {
      if (!pool.isShutdown())
        (*task)();
    }
  };

  /*
  struct DiscardOldestPolicy
  {
    void operator()(std::shared_ptr<Task> task, ThreadPool &pool)
    {}
  }; 
  */

  // ThreadPool is non-copyable.
  ThreadPool(const ThreadPool &) = delete;
  ThreadPool &operator=(const ThreadPool &) = delete;


  /// All workers join.
  virtual ~ThreadPool()
  {
    shutdown();
    worker_manager_.waitAllWorkersJoin();
  }

   
  /// Creates a new ThreadPool with the given initial parameters.
  ///
  /// \param core_pool_size The number of threads to keep in the pool, even
  ///        if they are idle, unless allowCoreThreadTimeout is set.
  /// \param max_pool_size The maximum number of threads to allow in the
  ///        pool.
  /// \param keep_alive_time When the number of threads is greater than
  ///        the core, this is the maximum time that excess idle threads
  ///        will wait for new tasks before terminating.
  /// \param task_queue The queue to use for holding tasks before they are
  ///        executed. This queue will hold only the Task
  ///        tasks submitted by the execute method.
  /// \param handler The handler to use when execution is blocked
  ///        because the thread bounds and queue capacities are reached.
  template <typename Rep, typename Period>
  ThreadPool(unsigned core_pool_size,
             unsigned max_pool_size,
             const std::chrono::duration<Rep, Period> &keep_alive_time,
             const BlockingQueue &task_queue,
             const rejected_execution_handler_type &handler = AbortPolicy())
      : ctl_(ctlOf(RUNNING, 0)), core_pool_size_(core_pool_size), max_pool_size_(max_pool_size),
        keep_alive_time_(keep_alive_time), task_queue_(task_queue), handler_(handler)
  {
    if (core_pool_size <= 0 || max_pool_size <= 0 || max_pool_size < core_pool_size || keep_alive_time.count() < 0)
      throw std::invalid_argument("Invalid argument");

    worker_manager_.run(*this);
  }

  /// \copydoc ThreadPool::ThreadPool(unsigned core_pool_size,
  ///                                 unsigned max_pool_size,
  ///                                 const std::chrono::duration<Rep, Period> &keep_alive_time,
  ///                                 const BlockingQueue &task_queue,
  ///                                 const rejected_execution_handler_type &handler = AbortPolicy())
  template <typename Rep, typename Period>
  ThreadPool(unsigned core_pool_size,
             unsigned max_pool_size,
             const std::chrono::duration<Rep, Period> &keep_alive_time,
             BlockingQueue &&task_queue,
             const rejected_execution_handler_type &handler = AbortPolicy())
      : ctl_(ctlOf(RUNNING, 0)), core_pool_size_(core_pool_size), max_pool_size_(max_pool_size),
        keep_alive_time_(keep_alive_time), task_queue_(std::move(task_queue)), handler_(handler)
  {
    if (core_pool_size <= 0 || max_pool_size <= 0 || max_pool_size < core_pool_size || keep_alive_time.count() < 0)
      throw std::invalid_argument("Invalid argument");

    worker_manager_.run(*this);
  }

  /// Execute a task.
  ///
  /// \param fn A functor.
  /// \param args Arguments of the functor.
  void execute(std::shared_ptr<Task> task)
  {
    if (task == nullptr)
    {
      throw NullPointerException("Null task pointer");
    }

    // Proceed in 3 steps:
    //
    // 1. If fewer than corePoolSize threads are running, try to
    // start a new thread with the given command as its first
    // task.  The call to addWorker atomically checks runState and
    // workerCount, and so prevents false alarms that would add
    // threads when it shouldn't, by returning false.
    //
    // 2. If a task can be successfully queued, then we still need
    // to double-check whether we should have added a thread
    // (because existing ones died since last checking) or that
    // the pool shut down since entry into this method. So we
    // recheck state and if necessary roll back the enqueuing if
    // stopped, or start a new thread if there are none.
    //
    // 3. If we cannot queue task, then we try to add a new
    // thread.  If it fails, we know we are shut down or saturated
    // and so reject the task.

    unsigned c = ctl_.load();
    if (workerCountOf(c) < core_pool_size_)
    {
      if (addWorker(task, true))
        return;
      c = ctl_.load();
    }
    if (isRunning(c) && task_queue_.offer(task))
    {
      unsigned recheck = ctl_.load();
      if (!isRunning(recheck) && remove(task))
        reject(task);
      else if (workerCountOf(recheck) == 0)
        addWorker(nullptr, false);
    }
    else if (!addWorker(task, false))
      reject(task);
  }

  /// Execute a task.
  ///
  /// \param fn A functor.
  /// \param args Arguments of the functor.
  template <typename Fn, typename... Args>
  void execute(Fn &&fn, Args &&...args)
  {
    auto task = createTask<RunnableTask>(std::forward<Fn>(fn), std::forward<Args>(args)...);

    execute(static_cast<std::shared_ptr<Task>>(task)); 
  }


  /// Execute a task with returning future.
  ///
  /// \param fn A functor.
  /// \param args Arguments of the functor.
  /// \return A future of type fn(args...).
  template <typename Fn, typename... Args>
  std::future<invoke_result_t<Fn, Args...>>
  submit(Fn &&fn, Args &&...args)
  {
    auto task = createTask<CallableTask>(std::forward<Fn>(fn), std::forward<Args>(args)...);

    execute(static_cast<std::shared_ptr<Task>>(task));

    return task->getFuture();
  }

  /// Initiates an orderly shutdown in which previously submitted
  /// tasks are executed, but no new tasks will be accepted.
  /// Invocation has no additional effect if already shut down.
  /// This method does not wait for previously submitted tasks to
  /// complete execution. Use awaitTermination to do that.
  void shutdown()
  {
    {
      boost::lock_guard<decltype(main_lock_)> lock(main_lock_);

      // checkShutdownAccess();
      advanceRunState(SHUTDOWN);
      interruptIdleWorkers();
      onShutdown();
    }

    tryTerminate();
  }

  /// Returns true if this pool allows core threads to time out and
  /// terminate if no tasks arrive within the keepAlive time, being
  /// replaced if needed when new tasks arrive. When true, the same
  /// keep-alive policy applying to non-core threads applies also to
  /// core threads. When false (the default), core threads are never
  /// terminated due to lack of incoming tasks.
  ///
  /// \return True if core threads are allowed to time out,
  ///         else false.
  bool allowCoreThreadTimeOut()
  {
    return allow_core_thread_timeout_;   
  }

  /// Sets the policy governing whether core threads may time out and
  /// terminate if no tasks arrive within the keep-alive time, being
  /// replaced if needed when new tasks arrive. When false, core
  /// threads are never terminated due to lack of incoming
  /// tasks. When true, the same keep-alive policy applying to
  /// non-core threads applies also to core threads. To avoid
  /// continual thread replacement, the keep-alive time must be
  /// greater than zero when setting true. This method
  /// should in general be called before the pool is actively used.
  ///
  /// \param value True if should time out, else false.
  /// \throws std::invalid_argument if value is true
  ///         and the current keep-alive time is not greater than zero.
  void allowCoreThreadTimeOut(bool value)
  {
    if(value && keep_alive_time_.count() <= 0)
      throw std::invalid_argument("Core threads must have nonzero keep alive times");
    if(value != allow_core_thread_timeout_)
    {
      allow_core_thread_timeout_ = value;
      if(value)
        interruptIdleWorkers();
    }
  }

  /// Sets the maximum allowed number of threads. This overrides any
  /// value set in the constructor. If the new value is smaller than
  /// the current value, excess existing threads will be
  /// terminated when they next become idle.
  ///
  /// \param max_pool_size The new maximum allowed number of threads.
  /// \throws std::invalid_argument if the new maximum is
  ///         less than or equal to zero, or
  ///         less than the core pool size.
  /// \see getMaxPoolSize
  void setMaxPoolSize(unsigned max_pool_size)
  {
    if(max_pool_size <= 0u || max_pool_size < core_pool_size_)
      throw std::invalid_argument("Invalid maximum pool size");
    this->max_pool_size_ = max_pool_size;
    if(workerCountOf(ctl_.load()) > this->max_pool_size_)
      interruptIdleWorkers();
  }

  /// Returns the maximum allowed number of threads.
  ///
  /// \return The maximum allowed number of threads.
  /// \see setMaxPoolSize
  unsigned getMaxPoolSize()
  {
    return max_pool_size_;
  }

  /// Returns the current number of threads in the pool.
  ///
  /// \return The number of threads.
  unsigned getPoolSize()
  {
    boost::lock_guard<decltype(main_lock_)> lock(main_lock_);
    return runStateAtLeast(ctl_.load(), TIDYING) ? 0 : worker_manager_.workers_.size();
  }

  /// Returns the approximate number of threads that are actively
  /// executing tasks.
  ///
  /// \return The number of threads.
  unsigned getActiveCount()
  {
    unsigned n = 0u;
    boost::lock_guard<decltype(main_lock_)> lock(main_lock_);
    for(const auto &pair : worker_manager_.workers_)
    {
      const auto &worker = pair.second->worker_;
      if(worker->isLocked())
        ++n;
    }
    return n;
  }

  /// Returns the largest number of threads that have ever
  /// simultaneously been in the pool.
  ///
  /// \return The number of threads.
  unsigned getLargestPoolSize()
  {
    boost::lock_guard<decltype(main_lock_)> lock(main_lock_);
    return largest_pool_size_;
  }

  /// Returns the approximate total number of tasks that have ever been
  /// scheduled for execution. Because the states of tasks and
  /// threads may change dynamically during computation, the returned
  /// value is only an approximation.
  ///
  /// \return The number of tasks.
  unsigned long long getTaskCount()
  {
    boost::lock_guard<decltype(main_lock_)> lock(main_lock_);
    auto n = completed_task_count_;
    for(const auto &pair : worker_manager_.workers_)
    {
      const auto &worker = pair.second->worker_;
      n += worker->completed_tasks_;
      if(worker->isLocked())
        ++n;
    }
    return n+task_queue_.size();
  }

  /// Returns the approximate total number of tasks that have
  /// completed execution. Because the states of tasks and threads
  /// may change dynamically during computation, the returned value
  /// is only an approximation, but one that does not ever decrease
  /// across successive calls.
  ///
  /// \return the number of tasks
  unsigned long long getCompletedTaskCount()
  {
    boost::lock_guard<decltype(main_lock_)> lock(main_lock_);
    auto n = completed_task_count_;
    for(const auto &pair : worker_manager_.workers_)
    {
      const auto &worker = pair.second->data_;
      n += worker->completed_tasks_;
    }
    return n;
  }

/*
  void shutdownNow()
  {
    boost::lock_guard<decltype(main_lock_)> lock(main_lock_);
    advanceRunState(STOP);
    interruptWorkers();
  }
*/

  bool isShutdown()
  {
    return !isRunning(ctl_.load());
  }

  template <typename Rep, typename Period>
  bool awaitTermination(const std::chrono::duration<Rep, Period> &timeout)
  {
    boost::chrono::nanoseconds boost_timeout{std::chrono::duration_cast<std::chrono::nanoseconds>(timeout).count()};

    boost::unique_lock<decltype(main_lock_)> lock(main_lock_);
    for(;;)
    {
      if (runStateAtLeast(ctl_.load(), TERMINATED))
        return true;
      if (timeout.count() <= 0)
        return false;
      auto time_begin = boost::chrono::system_clock::now();
      auto status = termination_.wait_for(lock, boost_timeout);
      if(status == boost::cv_status::timeout)
        return false;
      auto time_end = boost::chrono::system_clock::now();
      boost_timeout -= time_end - time_begin;
    }
  }

  void onShutdown() {}

protected:
  virtual void beforeExecute(boost::thread &thread, std::shared_ptr<Task> task) {}
  virtual void afterExecute(std::shared_ptr<Task> task, std::exception_ptr eptr) {}
  virtual void terminated() {}

private:
  struct Worker
  {
    explicit Worker(std::shared_ptr<Task> task)
        : first_task_(std::move(task)), inuse_(-1)
    {
      // inhibit interrupts until runWorker
    }

    ~Worker()
    {
      if (thread_.joinable())
        thread_.join();
    }

    void run(ThreadPool &this_pool)
    {
      thread_ = boost::thread(&ThreadPool::runWorker, boost::ref(this_pool), this);
    }

    void interruptIfStarted()
    {
      if (inuse_.load() >= 0 && thread_.joinable() && !thread_.interruption_requested())
      {
        thread_.interrupt();
      }
    }

    void lock()
    {
      int old_val = 0;
      while(!inuse_.compare_exchange_weak(old_val, 1))
        ;
    }

    void unlock()
    {
      inuse_.store(0);
    }

    bool tryLock()
    {
      int old_val = 0;
      return inuse_.compare_exchange_weak(old_val, 1); 
    }

    bool tryUnlock()
    {
      // int old_val = 1;
      inuse_.store(0);
      // return inuse_.compare_exchange_weak(old_val, 0); 
      return true;
    }

    bool isLocked()
    {
      return inuse_.load() != 0;
    }

    unsigned long long completed_tasks_ = 0ull;
    std::shared_ptr<Task> first_task_;
    std::atomic_int inuse_;
    boost::thread thread_;
  }; 

  struct WorkerManager
  {
    using Node = typename detail::list<Worker>::Node;
    
    void run(ThreadPool &pool)
    {
      thread_ = boost::thread(&ThreadPool::joinDyingWorkers, boost::ref(pool), boost::ref(*this));
    }

    void add(std::shared_ptr<Worker> w)
    {
      std::shared_ptr<Node> node(new Node);
      node->data_ = w;
      workers_.insert(std::make_pair<Worker *, std::shared_ptr<Node>>(w.get(), std::move(node)));
    }

    void remove(std::shared_ptr<Worker> w)
    {
      workers_.erase(w.get());
    }

    void remove(Worker *w)
    {
      std::shared_ptr<Node> node = workers_[w];
      workers_.erase(w);
      // node.unique() == true && node->data_.unique() == true

      boost::lock_guard<decltype(mutex_)> lock(mutex_);
      dying_workers_.pushTail(std::move(node));

      cond_.notify_one();
    }

    void interruptIdleWorkers(bool only_one)
    {
      for(const auto &pair : workers_)
      {
        auto &worker = pair.second->data_;
        if(!worker->thread_.interruption_requested() && worker->tryLock())
        {
          worker->thread_.interrupt();
          worker->unlock();
        }
        if (only_one)
          break;
      }
    }

    void waitAllWorkersJoin()
    {
      {
        std::lock_guard<decltype(mutex_)> lock(mutex_);
        cond_.notify_one();
      }

      if(thread_.joinable())
        thread_.join();
    }

    unsigned size()
    {
      return workers_.size();
    }

    boost::mutex mutex_;
    boost::condition_variable cond_;
    boost::thread thread_;
    std::unordered_map<Worker *, std::shared_ptr<Node>> workers_;
    detail::list<Worker> dying_workers_;
  };

  /// Performs blocking or timed wait for a task, depending on
  /// current configuration settings, or returns null if this worker
  /// must exit because of any of:
  /// 1. There are more than maximumPoolSize workers (due to
  ///    a call to setMaximumPoolSize).
  /// 2. The pool is stopped.
  /// 3. The pool is shutdown and the queue is empty.
  /// 4. This worker timed out waiting for a task, and timed-out
  ///    workers are subject to termination (that is,
  ///    { \code allowCoreThreadTimeOut || workerCount > corePoolSize \endcode })
  ///    both before and after the timed wait, and if the queue is
  ///    non-empty, this worker is not the last thread in the pool.
  ///
  /// \return task, or null if the worker must exit, in which case
  ///         workerCount is decremented
  std::shared_ptr<Task> getTask()
  {
    bool timed_out = false; // Did the last poll() time out?

    for (;;)
    {
      unsigned c = ctl_.load();
      unsigned rs = runStateOf(c);

      // Check if queue empty only if necessary.
      if (rs >= SHUTDOWN && (rs >= STOP || task_queue_.isEmpty()))
      {
        decrementWorkerCount();
        return nullptr;
      }

      unsigned wc = workerCountOf(c);

      // Are workers subject to culling?
      bool timed = allow_core_thread_timeout_ || wc > core_pool_size_;

      if ((wc > max_pool_size_ || (timed && timed_out)) && (wc > 1 || task_queue_.isEmpty()))
      {
        if (compareAndDecrementWorkerCount(c))
          return nullptr;
        continue;
      }

      try
      {
        auto task = timed ? task_queue_.poll(keep_alive_time_) : task_queue_.take();
        if (task != nullptr)
          return task;
        timed_out = true;
      }
      catch (boost::thread_interrupted &retry)
      {
        timed_out = false;
      } 
    }
  }

  void runWorker(Worker *worker)
  {
    std::shared_ptr<Task> task = std::move(worker->first_task_); // worker->first_task_ is empty now.

    worker->unlock();

    bool completed_abruptly = true;

    auto finally1 = gsl::finally([&]{
      processWorkerExit(worker, completed_abruptly); 
    });

    while (task || (task = getTask()))
    {
      boost::lock_guard<Worker> lock(*worker);

      // Get interruption state and clear the state.
      auto interrupted = [] {
        bool is_interrupted = boost::this_thread::interruption_requested();
        try
        {
          boost::this_thread::interruption_point();
        }
        catch (boost::thread_interrupted &e)
        { }
        return is_interrupted;
      };

      if ((runStateAtLeast(ctl_.load(), STOP) ||
           (interrupted() && runStateAtLeast(ctl_.load(), STOP))) &&
          !worker->thread_.interruption_requested())
        worker->thread_.interrupt();

      {
        auto finally2 = gsl::finally([&]
        {
          task.reset();
          ++worker->completed_tasks_;
        });

        beforeExecute(worker->thread_, task);
        // Throwable thrown;

        {
          std::exception_ptr eptr;

          auto finally3 = gsl::finally([&] {
            afterExecute(task, eptr);
          });

          try
          {
            (*task)();
          }
          catch (...)
          {
            eptr = std::current_exception();
          }

          // finally3 executes here
          //
          // afterExecute(task, eptr);
        }

        // finally2 executes here.
        //
        // task.reset();
        // ++worker->completed_tasks_;
      }
    }

    completed_abruptly = false;

    // finally1 executes here.
    //
    // processWorkerExit(worker, completed_abruptly); 
  }
  
  bool addWorker(std::shared_ptr<Task> first_task, bool core)
  {
    // retry:
    for (;;)
    {
      unsigned c = ctl_.load();
      unsigned rs = runStateOf(c);

      // Check if queue empty only if necessary.
      if (rs >= SHUTDOWN &&
          !(rs == SHUTDOWN &&
            first_task == nullptr &&
            !task_queue_.isEmpty()))
        return false;

      for (;;)
      {
        unsigned wc = workerCountOf(c);
        if (wc >= CAPACITY ||
            wc >= (core ? core_pool_size_ : max_pool_size_))
          return false;
        if (compareAndIncrementWorkerCount(c))
          goto done; // break retry;
        c = ctl_.load(); // Re-read ctl
        if (runStateOf(c) != rs)
          break; // continue retry;
        // else CAS failed due to workerCount change; retry inner loop
      }
    }
  done:

    bool worker_started = false;
    bool worker_added = false;
    std::shared_ptr<Worker> w; // nullptr
    try
    {
      w.reset(new Worker(first_task));
      // w->run(*this, w);
      // boost::thread t = w.thread;
      try
      {
        boost::lock_guard<decltype(main_lock_)> lock(main_lock_);

        // Recheck while holding lock.
        // Back out on ThreadFactory failure or if
        // shut down before lock acquired.
        unsigned rs = runStateOf(ctl_.load());

        if (rs < SHUTDOWN ||
            (rs == SHUTDOWN && first_task == nullptr))
        {
          worker_manager_.add(w);
          unsigned s = worker_manager_.size();
          if (s > largest_pool_size_)
            largest_pool_size_ = s;
          worker_added = true;
        }
      }
      catch (...)
      {}

      if (worker_added)
      {
        w->run(*this);
        worker_started = true;
      }
    }
    catch (...)
    {}

    if (!worker_started)
      addWorkerFailed(w);
    return worker_started;
  }

  void addWorkerFailed(std::shared_ptr<Worker> w)
  {
    boost::lock_guard<decltype(main_lock_)> lock(main_lock_);
    try
    {
      if (w != nullptr)
        worker_manager_.remove(w);
      decrementWorkerCount();
      tryTerminate();
    }
    catch (...)
    {}
  }

  void interruptIdleWorkers(bool only_one)
  {
    boost::lock_guard<decltype(main_lock_)> lock(main_lock_);
    worker_manager_.interruptIdleWorkers(only_one);
  }

  void interruptIdleWorkers()
  {
    interruptIdleWorkers(false);
  }

  void interruptWorkers()
  {
    std::lock_guard<decltype(main_lock_)> lock(main_lock_);
    for (const auto &pair : worker_manager_.workers_)
    {
      auto &worker = pair.second->data_;
      worker.interruptIfStarted();
    }
  }

  /// Transitions to TERMINATED state if either (SHUTDOWN and pool
  /// and queue empty) or (STOP and pool empty).  
  /// If otherwise eligible to terminate but workerCount is nonzero, 
  /// interrupts an idle worker to ensure that shutdown signals propagate. 
  /// This method must be called following any action that might make
  /// termination possible -- reducing worker count or removing tasks
  /// from the queue during shutdown. The method is non-private to
  /// allow access from ScheduledThreadPoolExecutor.
  void tryTerminate()
  {
    for (;;)
    {
      unsigned c = ctl_.load();
      if (isRunning(c) ||
          runStateAtLeast(c, TIDYING) ||
          (runStateOf(c) == SHUTDOWN && !task_queue_.isEmpty()))
        return;
      if (workerCountOf(c) != 0)
      { 
        // Eligible to terminate
        constexpr bool ONLY_ONE = true;
        interruptIdleWorkers(ONLY_ONE);
        return;
      }

      boost::lock_guard<decltype(main_lock_)> lock(main_lock_);

      // If worker_manager_.size() > 0, that means some workers are not joined.
      if(worker_manager_.size() > 0)
        return ;

      if (ctl_.compare_exchange_weak(c, ctlOf(TIDYING, 0)))
      {
        auto finally = gsl::finally([&]{
          ctl_.store(ctlOf(TERMINATED, 0));
          termination_.notify_all();
        });

        // May throw exception.
        terminated();

        return;
      }
       // else retry on failed CAS
    }
  }

  void processWorkerExit(Worker *w, bool completed_abruptly)
  {
    if (completed_abruptly) // If abrupt, then workerCount wasn't adjusted
      decrementWorkerCount();

    {
      boost::lock_guard<decltype(main_lock_)> lock(main_lock_);
      completed_task_count_ += w->completed_tasks_;
      worker_manager_.remove(w);
    }

    tryTerminate();

    unsigned c = ctl_.load();
    if (runStateLessThan(c, STOP))
    {
      if (!completed_abruptly)
      {
        // unsigned min = allowCoreThreadTimeOut ? 0 : core_pool_size_;
        unsigned min = false ? 0u : core_pool_size_;
        if (min == 0u && !task_queue_.isEmpty())
          min = 1u;
        if (workerCountOf(c) >= min)
          return; // replacement not needed
      }
      addWorker(nullptr, false);
    }
  }

  void joinDyingWorkers(WorkerManager &manager)
  {
    auto &dying_workers = manager.dying_workers_;
    
    boost::unique_lock<decltype(manager.mutex_)> lock(manager.mutex_);

    for (;;)
    {
      while (runStateLessThan(ctl_.load(), TIDYING) && dying_workers.empty())
        manager.cond_.wait(lock);

      while (!dying_workers.empty())
      {
        auto node = dying_workers.popHead();

        assert(node.unique() && node->data_.unique() && "Worker holder is not unique!");

        // unlock here
        lock.unlock();

        try
        {
          node.reset(); // delete the worker
        }
        catch (...)
        { }

        // lock again
        lock.lock();
      }

      // If the state is advanced to TIDYING, all workers have been enqueued to dead_workers.
      // When dead_workers is also empty, all threads are joined and the thread pool can 
      // release safely.
      if (runStateAtLeast(ctl_.load(), TIDYING)) 
        return;
    }
  }

  void reject(std::shared_ptr<Task> task)
  {
    handler_(task, *this);
  }

  bool remove(std::shared_ptr<Task> task)
  {
    return false;
  }

  std::atomic_uint ctl_;
  static constexpr unsigned COUNT_BITS = std::numeric_limits<unsigned>::digits - 3u;
  static constexpr unsigned CAPACITY = (1u << COUNT_BITS) - 1u;

  static constexpr unsigned RUNNING    =      0u << COUNT_BITS;
  static constexpr unsigned SHUTDOWN   =  0b100u << COUNT_BITS;
  static constexpr unsigned STOP       =  0b101u << COUNT_BITS;
  static constexpr unsigned TIDYING    =  0b110u << COUNT_BITS;
  static constexpr unsigned TERMINATED =  0b111u << COUNT_BITS;

  static constexpr unsigned runStateOf(unsigned c) noexcept { return c & ~CAPACITY; }
  static constexpr unsigned workerCountOf(unsigned c) noexcept { return c & CAPACITY; }
  static constexpr unsigned ctlOf(unsigned rs, unsigned wc) noexcept { return rs | wc; }

  static constexpr bool runStateLessThan(unsigned c, unsigned s) noexcept { return c < s; }
  static constexpr bool runStateAtLeast(unsigned c, unsigned s) noexcept { return c >= s; }
  static constexpr bool isRunning(unsigned c) noexcept { return c < SHUTDOWN; }

  bool compareAndIncrementWorkerCount(unsigned expect)
  {
    return ctl_.compare_exchange_weak(expect, expect + 1u);
  }

  bool compareAndDecrementWorkerCount(unsigned expect)
  {
    return ctl_.compare_exchange_weak(expect, expect - 1u);
  }
  
  void decrementWorkerCount()
  {
    do
    { }
    while (!compareAndDecrementWorkerCount(ctl_.load()));
  }

  void advanceRunState(unsigned targetState)
  {
    for (;;)
    {
      unsigned c = ctl_.load();
      if (runStateAtLeast(c, targetState) ||
          ctl_.compare_exchange_weak(c, ctlOf(targetState, workerCountOf(c))))
        break;
    }
  }

  unsigned core_pool_size_;
  unsigned max_pool_size_;
  unsigned largest_pool_size_ = 0u;
  std::chrono::nanoseconds keep_alive_time_;

  boost::recursive_mutex main_lock_;
  boost::condition_variable_any termination_;
  BlockingQueue task_queue_;
  WorkerManager worker_manager_;
  rejected_execution_handler_type handler_;

  unsigned long long completed_task_count_ = 0ull;

  bool allow_core_thread_timeout_ = false;
};

} // namespace cxxthreads

#endif // CXXTHREADS_THREADPOOL_H