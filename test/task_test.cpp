#include <gtest/gtest.h>

#include "cxxthreads/task.h"

using cxxthreads::RunnableTask;
using cxxthreads::is_callable;

static int global_val;
static void globalSetVal() { global_val = 1; }

TEST(RunnableTaskTest, InvokeNormalFunctionTask)
{
  global_val = 0;
  ASSERT_EQ(global_val, 0);

  RunnableTask<void(*)()> task(globalSetVal);
  task();
  ASSERT_EQ(global_val, 1);
}


TEST(RunnableTaskTest, InvokeLambdaTask)
{
  int result = 0;
  ASSERT_EQ(result, 0);

  auto r = [&result](){ result = 1; };
  RunnableTask<decltype(r)> task(std::move(r));
  task();
  ASSERT_EQ(result, 1);
}


TEST(RunnableTaskTest, InvokeBoundFunctionTask)
{
  struct Object {
    Object():val_(0){}
    void setVal() { val_ = 1;}

    int val_;
  };

  Object obj;
  ASSERT_EQ(obj.val_, 0);

  auto r = std::bind(&Object::setVal, std::ref(obj));
  RunnableTask<decltype(r)> task(std::move(r));
  task();
  EXPECT_EQ(obj.val_, 1);
}


static void globalF1() {}
static int globalF2() { return 0; }
static void globalF3(int) {}
TEST(is_callable_test, GlobalFunction) 
{
  ASSERT_EQ(is_callable<decltype(&globalF1)>::value, true);
  ASSERT_EQ(is_callable<decltype(&globalF2)>::value, true);
  ASSERT_EQ(is_callable<decltype(&globalF3)>::value, false);
}


TEST(is_callable_test, Lambda) 
{
  auto f1 = [](){};
  ASSERT_EQ(is_callable<decltype(f1)>::value, true);

  auto f2 = [](){return 1;};
  ASSERT_EQ(is_callable<decltype(f2)>::value, true);

  auto f3 = [](int){};
  ASSERT_EQ(is_callable<decltype(f3)>::value, false);
}


TEST(is_callable_test, CallableObject) 
{
  struct Object1
  {
    void operator()() {}
  };
  ASSERT_EQ(is_callable<Object1>::value, true);
  
  struct Object2
  {
    void operator()() const {}
  };
  ASSERT_EQ(is_callable<Object2>::value, true);

  struct Object3
  {
    void operator()() volatile {}
  };
  ASSERT_EQ(is_callable<Object3>::value, true);

  struct Object4
  {
    void operator()() const volatile {}
  };
  ASSERT_EQ(is_callable<Object4>::value, true);

  struct Object5
  {
    int operator()() { return 0; }
  };
  ASSERT_EQ(is_callable<Object5>::value, true);

  struct Object6
  {
    void operator()(int) {}
  };
  ASSERT_EQ(is_callable<Object6>::value, false);

  struct Object7
  {};
  ASSERT_EQ(is_callable<Object7>::value, false);
}