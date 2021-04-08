#include <gtest/gtest.h>

#include "cxxthreads/threadpool.h"

#include <memory>
#include <vector>

using cxxthreads::ThreadPool;
using cxxthreads::BlockingQueue;
using cxxthreads::Task;

TEST(ThreadPoolTest, CreateAndDeletePool)
{
  for (int i = 0; i < 100; ++i)
  {
    BlockingQueue<std::shared_ptr<Task>> q;
    using ThreadPoolType = ThreadPool<BlockingQueue<std::shared_ptr<Task>>>;
    std::shared_ptr<ThreadPoolType> pool(new ThreadPoolType(5, 5, std::chrono::nanoseconds(2), std::move(q)));
  }
}

TEST(ThreadPoolTest, ExecuteTest)
{
  BlockingQueue<std::shared_ptr<Task>> q;
  using ThreadPoolType = ThreadPool<BlockingQueue<std::shared_ptr<Task>>>;
  std::shared_ptr<ThreadPoolType> pool(new ThreadPoolType(5, 5, std::chrono::nanoseconds(2), std::move(q)));

  int total = 5000;
  std::vector<int> result(total, 0);
  for(int i=0; i<total; ++i)
  {
    pool->execute([&]{
      boost::chrono::microseconds t(1);
      boost::this_thread::sleep_for(t);
      result[i] = i;
    });
  }

  pool->shutdown();

  std::chrono::seconds t(1);
  while(!pool->awaitTermination(t))
    ;  
  
  ASSERT_EQ(pool->getCompletedTaskCount(), total);
}

TEST(ThreadPoolTest, SubmitTest)
{
  BlockingQueue<std::shared_ptr<Task>> q;
  using ThreadPoolType = ThreadPool<BlockingQueue<std::shared_ptr<Task>>>;
  std::shared_ptr<ThreadPoolType> pool(new ThreadPoolType(5, 5, std::chrono::nanoseconds(2), std::move(q)));

  ASSERT_EQ(pool->getCompletedTaskCount(), 0u);

  int total = 10000;
  std::vector<std::future<int>> v(total);
  for (int i = 0; i < total; ++i)
  {
    v[i] = pool->submit(
        [](int i) {
          return i;
        },
        i);
  }

  for (int i = 0; i < total; ++i)
  {
    EXPECT_EQ(v[i].get(), i);
  }

  ASSERT_EQ(pool->getCompletedTaskCount(), total);

  pool.reset();
}