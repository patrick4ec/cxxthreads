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

#ifndef CXXTHREADS_CONCURRENTQUEUE_H
#define CXXTHREADS_CONCURRENTQUEUE_H

#include "concurrentqueue/concurrentqueue.h"

namespace cxxthreads 
{

template<typename T>
class BlockingQueue 
{
public:
  using size_type = std::size_t;
  static constexpr size_type MAX_CAPACITY = std::numeric_limits<size_type>::max() - 1;

  BlockingQueue(size_type cap = MAX_CAPACITY) : cap_(cap), size_(0u)
  {
    if (cap <= 0u)
      throw std::invalid_argument("Invalid capacity: 0");
  }

  BlockingQueue(const BlockingQueue &) = delete;
  BlockingQueue &operator=(const BlockingQueue &) = delete;

  BlockingQueue(BlockingQueue &&other)
      : cap_(other.cap_), size_(static_cast<size_type>(other.size_)), queue_(std::move(other.queue_))
  {}

  BlockingQueue &operator=(BlockingQueue &&other)
  {
    cap_ = other.cap_;
    size_ = static_cast<size_type>(other.size_);
    std::swap(queue_, other.queue_);
  }

  ~BlockingQueue() = default;

  bool tryEnqueue(const T &e)
  {
    size_type size = size_;
    size_type expect = size + 1;
    if (expect > this->cap_)
      return false;
    if (!size_.compare_exchange_weak(size, expect))
      return false;
    if (!queue_.try_enqueue(e))
    {
      --size_;
      return false;
    }

    return true;
  }

  bool tryEnqueue(T &&e)
  {
    size_type size = size_;
    size_type expect = size + 1;
    if (expect > this->cap_)
      return false;
    if (!size_.compare_exchange_weak(size, expect))
      return false;
    if (!queue_.try_enqueue(std::move(e)))
    {
      --size_;
      return false;
    }

    return true;
  }

  bool tryDequeue(T &e)
  {
    size_type size = size_;
    if (size <= 0)
      return false;
    size_type expect = size - 1;
    if (!size_.compare_exchange_weak(size, expect))
      return false;
    if (!queue_.try_dequeue(e))
    {
      ++size_;
      return false;
    }

    return true;
  }

  constexpr bool remove(const T &) const noexcept
  {
    return false;
  }

  bool isEmpty()
  {
    return size_.load() <= 0u;
  }

  bool offer(T &&e)
  {
    while (size_.load() < cap_)
      if (tryEnqueue(std::move(e)))
        return true;
    return false;
  }

  bool offer(const T &e)
  {
    while (size_.load() < cap_)
      if (tryEnqueue(e))
        return true;
    return false;
  }

  size_type size()
  {
    return size_.load();
  }

private:
  size_type cap_; // capacity
  std::atomic_size_t size_;
  moodycamel::ConcurrentQueue<T> queue_;
}; 

} // namespace cxxthreads;

#endif // CXXTHREADS_CONCURRENTQUEUE_H