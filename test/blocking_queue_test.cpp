#include "gtest/gtest.h"

#include "cxxthreads/blocking_queue.h"


TEST(BlockingQueueTest, EnqueueAndDequeueWorks) 
{
  cxxthreads::BlockingQueue<int> q(10);
  for (int i = 1; i <= 10; ++i)
  {
    EXPECT_EQ(q.tryEnqueue(i), true);
    EXPECT_EQ(q.size(), i);
  }
  EXPECT_EQ(q.tryEnqueue(11), false);
  
  int n;
  for (int i = 1; i <= 10; ++i)
  {
    EXPECT_EQ(q.tryDequeue(n), true);
  }
  EXPECT_EQ(q.tryDequeue(n), false);
}

TEST(BlockingQueueTest, MultiThreadTest)
{
  cxxthreads::BlockingQueue<int> q(1000);

  std::atomic_uint total{0};

  std::thread th1, th2, th3, th4;
  auto enqueue_task = [&q, &total]
  {
    for(int i=0; i<100; ++i)
    {
      if(q.tryEnqueue(i))
      {
        ++total;
      }
    }
  };
  th1 = std::thread(enqueue_task);
  th2 = std::thread(enqueue_task);

  th1.join();
  th2.join();

  ASSERT_EQ(q.size(), static_cast<int>(total));

  auto dequeue_task = [&q, &total]
  {
    while(static_cast<int>(total) > 0)
    {
      int n;
      if(q.tryDequeue(n))
      {
        --total;
      }
    }
  };

  th3 = std::thread(dequeue_task);
  th4 = std::thread(dequeue_task);

  th3.join();
  th4.join();

  ASSERT_EQ(q.size(), 0);
}