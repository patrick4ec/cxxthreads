#include "gtest/gtest.h"

#include "cxxthreads/threadpool.h"

#include <memory>
#include <vector>

using cxxthreads::ThreadPool;
using cxxthreads::BlockingQueue;
using cxxthreads::Task;

TEST(ThreadPoolTest, SubmitTest)
{
    using ThreadPoolType = ThreadPool<BlockingQueue<std::shared_ptr<Task>>>;
    std::shared_ptr<ThreadPoolType> pool = std::make_shared<ThreadPoolType>(5, 10, std::chrono::nanoseconds(2), BlockingQueue<std::shared_ptr<Task>>());

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