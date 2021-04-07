#include <gtest/gtest.h>

#include "cxxthreads/threadpool.h"

#include <memory>
#include <vector>

using cxxthreads::ThreadPool;
using cxxthreads::BlockingQueue;
using cxxthreads::Task;

TEST(ThreadPoolTest, CreateAndDeletePool)
{
  for (int i = 0; i < 10; ++i)
  {
    BlockingQueue<std::shared_ptr<Task>> q;
    using ThreadPoolType = ThreadPool<BlockingQueue<std::shared_ptr<Task>>>;
    std::shared_ptr<ThreadPoolType> pool(new ThreadPoolType(5, 5, std::chrono::nanoseconds(2), std::move(q)));
  }
}

TEST(ThreadPoolTest, CompletedTaskCount)
{
  BlockingQueue<std::shared_ptr<Task>> q;
  using ThreadPoolType = ThreadPool<BlockingQueue<std::shared_ptr<Task>>>;
  std::shared_ptr<ThreadPoolType> pool(new ThreadPoolType(5, 5, std::chrono::nanoseconds(2), std::move(q)));

  ASSERT_EQ(pool->getCompletedTaskCount(), 0u);
}

TEST(ThreadPoolTest, SubmitTest)
{
  BlockingQueue<std::shared_ptr<Task>> q;
  using ThreadPoolType = ThreadPool<BlockingQueue<std::shared_ptr<Task>>>;
  std::shared_ptr<ThreadPoolType> pool(new ThreadPoolType(5, 5, std::chrono::nanoseconds(2), std::move(q)));

  std::vector<std::future<int>> v(10000);
  for (int i = 0; i < 10000; ++i)
  {
    v[i] = pool->submit(
        [](int i) {
          return i;
        },
        i);
  }

  for (int i = 0; i < 10000; ++i)
  {
    EXPECT_EQ(v[i].get(), i);
  }

  pool.reset();
}