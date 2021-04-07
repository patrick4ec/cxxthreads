#include <gtest/gtest.h>

#include "cxxthreads/blocking_queue.h"

#include <boost/thread.hpp>

using cxxthreads::BlockingQueue;

TEST(BlockingQueueTest, CapacityAndInitialSize) 
{
  constexpr unsigned capacity = 1000u;
  BlockingQueue<std::shared_ptr<int>> q{capacity};
  ASSERT_EQ(q.capacity(), capacity);
  ASSERT_EQ(q.size(), 0u);
}

TEST(BlockingQueueTest, OfferAndPoll1) 
{
  constexpr unsigned capacity = 1000u;
  BlockingQueue<std::shared_ptr<int>> q{capacity};

  for(unsigned i=1u; i<=capacity; ++i)
  {
    q.offer(std::make_shared<int>(i));
    ASSERT_EQ(q.size(), i);
  }

  ASSERT_EQ(q.offer(std::make_shared<int>(capacity+1u)), false);

  for(unsigned i=1u; i<=capacity; ++i)
  {
    auto n = q.poll();
    ASSERT_EQ(*n, i);
    ASSERT_EQ(q.size(), capacity-i);
  }

  ASSERT_EQ(q.poll(), nullptr);
}

TEST(BlockingQueueTest, OfferAndPoll2) 
{
  // Capacity is at least greater than zero.
  constexpr unsigned capacity = 1u;
  BlockingQueue<std::shared_ptr<int>> q{capacity};
  q.offer(std::make_shared<int>(1));

  boost::thread th1([&] {
    std::chrono::milliseconds t(10);
    q.offer(std::make_shared<int>(2), t);
  });

  th1.join();
  ASSERT_EQ(q.size(), 1u);
}

TEST(BlockingQueueTest, OfferAndPoll3) 
{
  // Capacity is at least greater than zero.
  constexpr unsigned capacity = 1u;
  BlockingQueue<std::shared_ptr<int>> q{capacity};

  boost::thread th1([&] {
    std::chrono::milliseconds t(10);
    q.poll(t);
  });

  th1.join();
  ASSERT_EQ(q.size(), 0u);
}

TEST(BlockingQueueTest, OfferAndPoll4) 
{
  // Capacity is at least greater than zero.
  constexpr unsigned capacity = 1u;
  BlockingQueue<std::shared_ptr<int>> q{capacity};
  q.offer(std::make_shared<int>(1));

  boost::thread th1([&] {
    std::chrono::milliseconds t(10);
    EXPECT_THROW(q.offer(std::make_shared<int>(2), t), boost::thread_interrupted);
  });

  th1.interrupt();
  th1.join();
  ASSERT_EQ(q.size(), 1u);
}

TEST(BlockingQueueTest, OfferAndPoll5) 
{
  // Capacity is at least greater than zero.
  constexpr unsigned capacity = 1u;
  BlockingQueue<std::shared_ptr<int>> q{capacity};

  boost::thread th1([&] {
    std::chrono::milliseconds t(10);
    EXPECT_THROW(q.poll(t), boost::thread_interrupted);
  });

  th1.interrupt();
  th1.join();
  ASSERT_EQ(q.size(), 0u);
}

TEST(BlockingQueueTest, PutAndTake1) 
{
  constexpr unsigned capacity = 1000u;
  BlockingQueue<std::shared_ptr<int>> q{capacity};

  boost::thread th1([&] {
    for (unsigned i = 1u; i <= capacity; ++i) 
    {
      q.put(std::make_shared<int>(i));
      ASSERT_EQ(q.size(), i);
    }
  });

  th1.join();

  for(unsigned i=1u; i<=capacity; ++i)
  {
    auto n = q.take();
    ASSERT_EQ(*n, i);
    ASSERT_EQ(q.size(), capacity-i);
  }
}

TEST(BlockingQueueTest, PutAndTake2) 
{
  constexpr unsigned capacity = 1000u;
  BlockingQueue<std::shared_ptr<int>> q{capacity};

  boost::thread th1([&] {
    for (unsigned i = 1u; i < capacity; ++i)
      q.put(std::make_shared<int>(i));
  });

  for(unsigned i=1u; i<capacity; ++i)
  {
    auto n = q.take();
    ASSERT_EQ(*n, i);
  }

  th1.join();
}

TEST(BlockingQueueTest, PutAndTake3) 
{
  constexpr unsigned capacity = 1000u;
  BlockingQueue<std::shared_ptr<int>> q{capacity};

  boost::thread th1([&] {
    for (unsigned i = 1u; i < capacity; ++i)
    {
      auto n = q.take();
      ASSERT_EQ(*n, i);
    }
  });

  for(unsigned i=1u; i<capacity; ++i)
  {
    q.put(std::make_shared<int>(i));
  }

  th1.join();
}

TEST(BlockingQueueTest, PutAndTake4) 
{
  // Capacity is at least greater than zero.
  constexpr unsigned capacity = 1u;
  BlockingQueue<std::shared_ptr<int>> q{capacity};
  q.put(std::make_shared<int>(1));

  boost::thread th1([&] {
    ASSERT_THROW(q.put(std::make_shared<int>(1)), boost::thread_interrupted);
  });

  th1.interrupt();
  th1.join();
}

TEST(BlockingQueueTest, PutAndTake5) 
{
  constexpr unsigned capacity = 1000u;
  BlockingQueue<std::shared_ptr<int>> q{capacity};

  boost::thread th1([&] {
    ASSERT_THROW(q.take(), boost::thread_interrupted);
  });

  th1.interrupt();
  th1.join();
}