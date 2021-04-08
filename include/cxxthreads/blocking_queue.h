//===---------------- cxxthreads/blocking_queue.h ---------------*- C++ -*-===//
//
//  Under Apache License v2.0.
//  Author: Patrick
//
//===----------------------------------------------------------------------===//
//
// This file defines the BlockingQueue. 
//
//===----------------------------------------------------------------------===//

#ifndef CXXTHREADS_BLOCKINGQUEUE_H
#define CXXTHREADS_BLOCKINGQUEUE_H

#include <boost/thread/condition_variable.hpp>
#include <boost/thread/mutex.hpp>

#include <atomic>
#include <memory>


namespace cxxthreads 
{

template <typename T, typename SizeType = std::size_t>
struct LinkedBlockingQueueImpl
{
  static_assert(std::is_unsigned<SizeType>::value, "SizeType is not unsigned");

  using size_type = SizeType;
  static constexpr size_type MAX_CAPACITY = std::numeric_limits<size_type>::max() - 1;

  struct Node
  {
    std::shared_ptr<T> data_;
    std::unique_ptr<Node> next_;
  };

  std::unique_ptr<Node> popHead() noexcept
  {
    std::unique_ptr<Node> old_head = std::move(head_);
    head_ = std::move(old_head->next_);
    return old_head;
  }

  void pushTail(std::shared_ptr<T> e, std::unique_ptr<Node> new_node) noexcept
  {
    tail_->data_ = e;
    Node *const new_tail = new_node.get();
    tail_->next_ = std::move(new_node);
    tail_ = new_tail;
  }

  std::unique_ptr<Node> waitPopHead()
  {
    boost::unique_lock<decltype(head_mutex_)> head_lock(head_mutex_);
    while(size() == 0u)
      not_empty_cond_.wait(head_lock);

    auto old_head = popHead();
    size_type old_size = size_.fetch_sub(1u);
    if(old_size > 1u)
      not_empty_cond_.notify_one();
    head_lock.unlock();

    if(old_size == capacity())
      signalNotFull();

    return old_head;
  }

  std::unique_ptr<Node> tryPopHead()
  {
    boost::unique_lock<decltype(head_mutex_)> head_lock(head_mutex_);
    if (size() == 0u)
      return std::unique_ptr<Node>();

    auto old_head = popHead();
    size_type old_size = size_.fetch_sub(1u);
    if(old_size > 1u)
      not_empty_cond_.notify_one();
    head_lock.unlock();

    if(old_size == capacity())
      signalNotFull();

    return old_head;
  }

  template <typename Rep, typename Period>
  std::unique_ptr<Node> tryPopHead(const std::chrono::duration<Rep, Period> &timeout)
  {
    boost::chrono::nanoseconds boost_timeout{std::chrono::duration_cast<std::chrono::nanoseconds>(timeout).count()};

    boost::unique_lock<decltype(head_mutex_)> head_lock(head_mutex_);
    while(size() == 0u)
    {
      if(boost_timeout.count() <= 0)
        return std::unique_ptr<Node>();
      auto time_begin = boost::chrono::system_clock::now();
      boost::cv_status status = not_empty_cond_.wait_for(head_lock, boost_timeout);
      if(status == boost::cv_status::timeout)
        return std::unique_ptr<Node>();
      auto time_end = boost::chrono::system_clock::now();
      boost_timeout -= time_end - time_begin;
    }

    auto old_head = popHead();
    size_type old_size = size_.fetch_sub(1u);
    if(old_size > 1u)
      not_empty_cond_.notify_one();
    head_lock.unlock();

    if(old_size == capacity())
      signalNotFull();

    return old_head;
  }

  void waitPushTail(std::shared_ptr<T> e)
  {
    std::unique_ptr<Node> p(new Node);

    boost::unique_lock<boost::mutex> tail_lock(tail_mutex_);
    while (size() >= cap_)
      not_full_cond_.wait(tail_lock);

    pushTail(e, std::move(p));
    size_type old_size = size_.fetch_add(1u);
    if (old_size + 1u < capacity())
      not_full_cond_.notify_one();
    tail_lock.unlock();

    if(old_size == 0u)
      signalNotEmpty();
  }

  bool tryPushTail(std::shared_ptr<T> e)
  {
    std::unique_ptr<Node> p(new Node);

    boost::unique_lock<boost::mutex> tail_lock(tail_mutex_);
    if (size() >= cap_)
      return false;

    pushTail(e, std::move(p));
    size_type old_size = size_.fetch_add(1u);
    if (old_size + 1u < capacity())
      not_full_cond_.notify_one();
    tail_lock.unlock();

    if(old_size == 0u)
      signalNotEmpty();
    return true;
  }

  template <typename Rep, typename Period>
  bool tryPushTail(std::shared_ptr<T> e, const std::chrono::duration<Rep, Period> &timeout)
  {
    boost::chrono::nanoseconds boost_timeout{std::chrono::duration_cast<std::chrono::nanoseconds>(timeout).count()};

    std::unique_ptr<Node> p(new Node);

    boost::unique_lock<decltype(tail_mutex_)> tail_lock(tail_mutex_);
    while (size() == capacity())
    {
      if (boost_timeout.count() <= 0)
        return false;
      auto time_begin = boost::chrono::system_clock::now();
      auto status = not_full_cond_.wait_for(tail_lock, boost_timeout);
      if (status == boost::cv_status::timeout)
        return false;
      auto time_end = boost::chrono::system_clock::now();
      boost_timeout -= time_end - time_begin;
    }

    pushTail(e, std::move(p));
    size_type old_size = size_.fetch_add(1u);
    if (old_size + 1u < capacity())
      not_full_cond_.notify_one();
    tail_lock.unlock();

    if(old_size == 0u)
      signalNotEmpty();
    return true;
  }

  size_type size() const noexcept
  {
    return static_cast<size_type>(size_);
  }

  constexpr size_type capacity() const noexcept
  {
    return cap_;
  }

  void signalNotFull()
  {
    boost::lock_guard<decltype(tail_mutex_)> lock(tail_mutex_); 
    not_full_cond_.notify_one();
  }

  void signalNotEmpty()
  {
    boost::lock_guard<decltype(head_mutex_)> lock(head_mutex_); 
    not_empty_cond_.notify_one();
  }

  explicit LinkedBlockingQueueImpl(size_type cap = MAX_CAPACITY)
      : cap_(cap), size_(0u), head_(new Node), tail_(nullptr)
  {
    if (cap <= 0u)
      throw std::invalid_argument("Invalid capacity: 0");

    tail_ = head_.get();
  }

  ~LinkedBlockingQueueImpl() = default;

  LinkedBlockingQueueImpl(const LinkedBlockingQueueImpl &) = delete;
  LinkedBlockingQueueImpl &operator=(const LinkedBlockingQueueImpl &) = delete;

  
  size_type cap_; // capacity
  std::atomic<size_type> size_;
  std::unique_ptr<Node> head_;
  Node *tail_;
  boost::mutex head_mutex_;
  boost::mutex tail_mutex_;
  boost::condition_variable not_empty_cond_;
  boost::condition_variable not_full_cond_;
};


template <typename T>
class BlockingQueue;


template <typename T>
class BlockingQueue<std::shared_ptr<T>>
{
public:
  using value_type = std::shared_ptr<T>;
  using size_type = std::size_t;

  static constexpr size_type MAX_CAPACITY = LinkedBlockingQueueImpl<T, size_type>::MAX_CAPACITY;

private:
  using Node = typename LinkedBlockingQueueImpl<T, size_type>::Node;

  std::unique_ptr<LinkedBlockingQueueImpl<T, size_type>> impl_;

public:
  explicit BlockingQueue(size_type cap = MAX_CAPACITY)
      : impl_(new LinkedBlockingQueueImpl<T, size_type>(cap))
  { }

  ~BlockingQueue() = default;

  BlockingQueue(const BlockingQueue &) = delete;
  BlockingQueue &operator=(const BlockingQueue &) = delete;

  BlockingQueue(BlockingQueue &&rhs) : impl_(std::move(rhs.impl_))
  { }

  BlockingQueue &operator=(BlockingQueue &&rhs)
  {
    impl_ = std::move(rhs.impl_);
  }

  constexpr bool remove(const std::shared_ptr<T> &) const noexcept
  {
    return false;
  }

  bool isEmpty() const noexcept
  {
    return impl_->size() <= 0u;
  }

  size_type size() const noexcept
  {
    return impl_->size();
  }

  constexpr size_type capacity() const noexcept
  {
    return impl_->capacity();
  }

  void put(std::shared_ptr<T> e)
  {
    impl_->waitPushTail(e); 
  }

  bool offer(std::shared_ptr<T> e)
  {
    return impl_->tryPushTail(e);
  }
  
  template <typename Rep, typename Period>
  bool offer(std::shared_ptr<T> e, const std::chrono::duration<Rep, Period> &timeout)
  {
    return impl_->tryPushTail(e, timeout);
  }

  std::shared_ptr<T> take()
  {
    std::unique_ptr<Node> const old_head = impl_->waitPopHead();
    return old_head->data_;
  }

  std::shared_ptr<T> poll()
  {
    std::unique_ptr<Node> const old_head = impl_->tryPopHead();
    if(old_head != nullptr)
      return old_head->data_;
    return std::shared_ptr<T>{};
  }

  template <typename Rep, typename Period>
  std::shared_ptr<T> poll(const std::chrono::duration<Rep, Period> &timeout)
  {
    std::unique_ptr<Node> const old_head = impl_->tryPopHead(timeout);
    if(old_head != nullptr)
      return old_head->data_;
    return std::shared_ptr<T>{};
  }

  void swap(BlockingQueue &other)
  {
    std::swap(impl_, other.impl_);
  }
};

template<typename T> 
void swap(BlockingQueue<T> &x, BlockingQueue<T> &y)
{
  x.swap(y);
}

} // namespace cxxthreads;

#endif // CXXTHREADS_BLOCKINGQUEUE_H