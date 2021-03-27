//===------------------- cxxthreads/thread_pool.h ---------------*- C++ -*-===//
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
#include "rejected_execution_exception.h"
#include "task.h"
#include "util.h"

#include <atomic>
#include <queue>
#include <thread>
#include <unordered_set>


namespace cxxthreads 
{

/// \class ThreadPool
/// \brief A dynamic thread pool implementation.
/// \headerfile "cxxthreads/threadpool.h"
template<typename BlockingQueue>
class ThreadPool 
{
public:
  using rejected_execution_handler_type = std::function<void(std::shared_ptr<Task> task, ThreadPool &pool)>;

  struct AbortPolicy
  {
    void operator()(std::shared_ptr<Task> task, ThreadPool &pool)
    {
      throw RejectedExecutionException("Task rejected");
    }
  };

  struct DiscardPolicy
  {
    void operator()(std::shared_ptr<Task> task, ThreadPool &pool)
    {}
  };

  struct CallerRunPolicy
  {
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

  ThreadPool(const ThreadPool &) = delete;
  ThreadPool &operator=(const ThreadPool &) = delete;

  virtual ~ThreadPool()
  {
    shutdown();
    cleaner_.waitAllWorkersJoin();
  };

   
  /// \brief Creates a new ThreadPool with the given initial parameters.
  /// 
  /// \param core_pool_size the number of threads to keep in the pool, even
  ///        if they are idle, unless {@code allowCoreThreadTimeOut} is set
  /// \param max_pool_size the maximum number of threads to allow in the
  ///        pool
  /// \param keep_alive_time when the number of threads is greater than
  ///        the core, this is the maximum time that excess idle threads
  ///        will wait for new tasks before terminating.
  /// \param task_queue the queue to use for holding tasks before they are
  ///        executed.  This queue will hold only the {@code Runnable}
  ///        tasks submitted by the {@code execute} method.
  /// \param threadFactory the factory to use when the executor
  ///        creates a new thread
  /// \param handler the handler to use when execution is blocked
  ///        because the thread bounds and queue capacities are reached
  /// \throws IllegalArgumentException if one of the following holds:<br>
  ///         {@code corePoolSize < 0}<br>
  ///         {@code keepAliveTime < 0}<br>
  ///         {@code maximumPoolSize <= 0}<br>
  ///         {@code maximumPoolSize < corePoolSize}
  /// \throws NullPointerException if {@code workQueue}
  ///         or {@code threadFactory} or {@code handler} is null
   
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

    cleaner_.run(*this);
  }

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

    cleaner_.run(*this);
  }

  /// \fn void execute(std::shared_ptr<Task> task)
  /// \brief Execute a task.
  /// \param fn a functor
  /// \param args arguments of the functor
  void execute(std::shared_ptr<Task> task)
  {
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

  /// \fn void execute(Fn &&fn, Args &&...args)
  /// \brief Execute a task.
  /// \param fn a functor
  /// \param args arguments of the functor
  template <typename Fn, typename... Args>
  void execute(Fn &&fn, Args &&...args)
  {
    auto task = createTask<RunnableTask>(std::forward<Fn>(fn), std::forward<Args>(args)...);

    execute(static_cast<std::shared_ptr<Task>>(task)); 
  }


  /// \fn std::future<?> submit(Fn &&fn, Args &&...args)
  /// \brief Execute a task with returning future.
  /// \param fn a functor
  /// \param args arguments of the functor
  /// \return a future of type fn(args...)
  template <typename Fn, typename... Args>
  std::future<invoke_result_t<Fn, Args...>>
  submit(Fn &&fn, Args &&...args)
  {
    auto task = createTask<CallableTask>(std::forward<Fn>(fn), std::forward<Args>(args)...);

    execute(static_cast<std::shared_ptr<Task>>(task));

    return task->getFuture();
  }

  /// \brief Initiates an orderly shutdown in which previously submitted
  ///        tasks are executed, but no new tasks will be accepted.
  ///
  /// Invocation has no additional effect if already shut down.
  /// This method does not wait for previously submitted tasks to
  /// complete execution.  Use awaitTermination to do that.
  void shutdown()
  {
    {
      std::lock_guard<decltype(main_lock_)> lock(main_lock_);

      try
      {
        // checkShutdownAccess();
        advanceRunState(SHUTDOWN);
        // interruptIdleWorkers();
        onShutdown();
      }
      catch (...)
      {}
    }

    tryTerminate();
  }

  /// \brief  Transitions to TERMINATED state if either (SHUTDOWN and pool
  ///         and queue empty) or (STOP and pool empty).  
  ///
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

      std::lock_guard<decltype(main_lock_)> lock(main_lock_);

      // If workers_.size() > 0, that means some workers are not joined.
      if(workers_.size() > 0)
        return ;

      try
      {
        if (ctl_.compare_exchange_weak(c, ctlOf(TIDYING, 0)))
        {
          try
          {
            terminated();
          }
          catch(...) {
            ctl_.store(ctlOf(TERMINATED, 0));
            termination_.notify_all();
          }

          return;
        }
      }
      catch(...) 
      {}
       // else retry on failed CAS
    }
  }

  /// \brief Returns the largest number of threads that have ever
  ///        simultaneously been in the pool.
  /// \return the number of threads
  unsigned getLargestPoolSize()
  {
    std::lock_guard<decltype(main_lock_)> lock(main_lock_);
    return largest_pool_size_;
  }

  bool isShutdown()
  {
    return !isRunning(ctl_.load());
  }

  void onShutdown() {}


private:
  struct Worker
  {
    explicit Worker(std::shared_ptr<Task> task)
        : first_task_(std::move(task))
    {}

    ~Worker()
    {
      if (thread_.joinable())
        thread_.join();
    }

    void run(ThreadPool &this_pool)
    {
      thread_ = std::thread(&ThreadPool::runWorker, std::ref(this_pool), this);
    }

    unsigned long long completed_tasks_ = 0ull;
    std::shared_ptr<Task> first_task_;
    std::thread thread_;
  }; // struct Worker

  struct WorkerManager
  {
    void run(ThreadPool &pool)
    {
      thread_ = std::thread(&ThreadPool::runWorkerCleaner, std::ref(pool), std::ref(*this));
    }

    void remove(Worker *w, ThreadPool &pool)
    {
      auto it = pool.workers_.begin();
      while (it != pool.workers_.end() && it->get() != w)
        ++it;
      assert(it != pool.workers_.end() && "Try to remove a nonexistent worker!");

      std::lock_guard<decltype(mutex_)> lock(mutex_);
      dead_workers_.push(*it);
      pool.workers_.erase(it);
      cond_.notify_one();
    }
    
    void waitAllWorkersJoin()
    {
      if(thread_.joinable())
        thread_.join();
    }

    std::mutex mutex_;
    std::condition_variable cond_;
    std::thread thread_;
    std::queue<std::shared_ptr<Worker>> dead_workers_;
  };

  std::shared_ptr<Task> getTask()
  {
    bool timedOut = false; // Did the last poll() time out?

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

      if ((wc > max_pool_size_ || (timed && timedOut)) && (wc > 1 || task_queue_.isEmpty()))
      {
        if (compareAndDecrementWorkerCount(c))
          return nullptr;
        continue;
      }

      /* try
      {
        auto r = timed ? workQueue.poll(keep_alive_time_, TimeUnit.NANOSECONDS) : workQueue.take();
        if (r != nullptr)
          return r;
        timedOut = true;
      }
      catch (InterruptedException retry)
      {
        timedOut = false;
      } */
      auto nanos = keep_alive_time_;
      for (;;)
      {
        std::shared_ptr<Task> task;
        auto start_time = std::chrono::system_clock::now();
        while (runStateOf(ctl_.load()) < STOP && !task_queue_.isEmpty() && !task_queue_.tryDequeue(task))
          ;
        auto end_time = std::chrono::system_clock::now();

        if (task != nullptr)
          return task;

        if (runStateOf(ctl_.load()) >= SHUTDOWN)
        {
          break;
        }

        if (timed)
        {
          nanos -= end_time - start_time;
          if (nanos.count() <= 0)
          {
            timedOut = true;
            break;
          }
        }
      }
    }

  }

  void runWorker(Worker *worker)
  {
    std::shared_ptr<Task> task = std::move(worker->first_task_);
    worker->first_task_.reset();

    bool completed_abruptly = true;
    try
    {
      while (task || (task = getTask()))
      {
        try
        {
          // beforeExecute();

          (*task)();

          // afterExecute();
        }
        catch (...)
        {}

        task.reset();
        ++worker->completed_tasks_;
      }

      completed_abruptly = false;
    }
    catch (...)
    {}

    processWorkerExit(worker, completed_abruptly); 

    // Worker's lifetime ends here.
    // when runWorker ends, worker will be deleted from the memory.
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
          goto done; //break retry;
        c = ctl_.load(); // Re-read ctl
        if (runStateOf(c) != rs)
          break; //continue retry;
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
      //w->run(*this, w);
      // std::Thread t = w.thread;
      try
      {
        std::lock_guard<decltype(main_lock_)> lock(main_lock_);

        // Recheck while holding lock.
        // Back out on ThreadFactory failure or if
        // shut down before lock acquired.
        unsigned rs = runStateOf(ctl_.load());

        if (rs < SHUTDOWN ||
            (rs == SHUTDOWN && first_task == nullptr))
        {
          workers_.insert(w);
          unsigned s = workers_.size();
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
    std::lock_guard<decltype(main_lock_)> lock(main_lock_);
    try
    {
      if (w != nullptr)
        workers_.erase(w);
      decrementWorkerCount();
      tryTerminate();
    }
    catch (...)
    {}
  }

  void interruptIdleWorkers(bool only_one)
  {}

protected:
  virtual void beforeExecute(std::shared_ptr<Task> task, std::exception e) {}
  virtual void afterExecute(std::shared_ptr<Task> task, std::exception e) {}
  virtual void terminated() {}

private:
  void processWorkerExit(Worker *w, bool completed_abruptly)
  {
    if (completed_abruptly) // If abrupt, then workerCount wasn't adjusted
      decrementWorkerCount();

    {
      std::lock_guard<decltype(main_lock_)> lock(main_lock_);
      completed_task_count_ += w->completed_tasks_;
      cleaner_.remove(w, *this);
    }

    tryTerminate();

    unsigned c = ctl_.load();
    if (runStateLessThan(c, STOP))
    {
      if (!completed_abruptly)
      {
        //unsigned min = allowCoreThreadTimeOut ? 0 : core_pool_size_;
        unsigned min = false ? 0u : core_pool_size_;
        if (min == 0u && !task_queue_.isEmpty())
          min = 1u;
        if (workerCountOf(c) >= min)
          return; // replacement not needed
      }
      addWorker(nullptr, false);
    }
  }

  /**
   *  
   */
  void runWorkerCleaner(WorkerManager &cleaner)
  {
    auto &dead_workers = cleaner.dead_workers_;
    std::unique_lock<std::mutex> lock(cleaner.mutex_);

    for (;;)
    {
      while (runStateLessThan(ctl_.load(), TIDYING) && dead_workers.empty())
        cleaner.cond_.wait(lock);

      while (!dead_workers.empty())
      {
        std::shared_ptr<Worker> worker_holder = dead_workers.front();
        dead_workers.pop();

        // unlock here
        lock.unlock();

        assert(worker_holder.unique() && "Worker holder is not unique!");
        try
        {
          worker_holder.reset(); // delete the worker
        }
        catch (...)
        {}

        // lock again
        lock.lock();
      }

      // If the state is advanced to TIDYING, all workers have been enqueued to dead_workers.
      // When dead_workers is also empty, all threads are joined and the thread pool can 
      // releases safely.
      if (runStateAtLeast(ctl_.load(), TIDYING) && dead_workers.empty()) 
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
    {}
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

  std::recursive_mutex main_lock_;
  std::condition_variable termination_;
  std::unordered_set<std::shared_ptr<Worker>> workers_;
  BlockingQueue task_queue_;
  WorkerManager cleaner_;
  rejected_execution_handler_type handler_;

  unsigned long long completed_task_count_ = 0ull;

  bool allow_core_thread_timeout_ = false;
};

} // namespace cxxthreads

#endif // CXXTHREADS_THREADPOOL_H