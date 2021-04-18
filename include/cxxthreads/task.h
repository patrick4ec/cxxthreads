//===------------------------ cxxthreads/task.h -----------------*- C++ -*-===//
//
//  Under Apache License v2.0.
//  Author: Patrick
//
//===----------------------------------------------------------------------===//
//
// This file defines the some classes relative to the task. 
//
//===----------------------------------------------------------------------===//

#ifndef CXXTHREADS_TASK_H
#define CXXTHREADS_TASK_H

#include "util.h"

#include <functional>
#include <future>
#include <type_traits>

namespace cxxthreads
{

#if __cplusplus >= 201703L

using std::disjunction;
using std::conjuncton;
using std::negation;

#else // C++17

template <typename...>
struct disjunction;

template <>
struct disjunction <> : public std::false_type
{};

template <typename B1>
struct disjunction <B1> : public B1{};

template <typename B1, typename B2>
    struct disjunction <B1, B2>
    : public std::conditional<B1::value, B1, B2>::type
{};

template <typename B1, typename B2, typename B3, typename... Bn>
struct disjunction<B1, B2, B3, Bn...>
    : public std::conditional<B1::value, B1, disjunction<B2, B3, Bn...>>::type
{};

template<typename...>
struct conjunction;

template<>
struct conjunction<> : public std::true_type
{ };

template<typename B1>
struct conjunction<B1> : public B1
{ };

template<typename B1, typename B2>
struct conjunction<B1, B2>
: public std::conditional<B1::value, B2, B1>::type
{ };

template<typename B1, typename B2, typename B3, typename... Bn>
struct conjunction<B1, B2, B3, Bn...>
: public std::conditional<B1::value, conjunction<B2, B3, Bn...>, B1>::type
{ };

template <bool B>
using bool_constant = std::integral_constant<bool, B>;

template<typename Pp>
    struct negation 
    : public bool_constant<!bool(Pp::value)>
    { };

#endif // C++14

template <typename T>
struct type_identity
{
  using type = T;
};

template <typename T, size_t = sizeof(T)>
constexpr std::true_type
    is_complete_or_unbounded(type_identity<T>)
{
  return {};
}

template<typename T>
    struct is_array_known_bounds
    : public std::integral_constant<bool, (std::extent<T>::value > 0)>
    { };

template<typename T>
    struct is_array_unknown_bounds
    : public conjunction<std::is_array<T>, negation<std::extent<T>>>
    { };

template <typename _TypeIdentity,
          typename _NestedType = typename _TypeIdentity::type>
constexpr typename disjunction<
    std::is_reference<_NestedType>,
    std::is_function<_NestedType>,
    std::is_void<_NestedType>,
    is_array_unknown_bounds<_NestedType>>::type
    is_complete_or_unbounded(_TypeIdentity)
{
  return {};
} 

// A metafunction that always yields void, used for detecting valid types.
template <typename...>
using void_t = void;

template <typename Result, typename = void>
struct is_callable_impl : std::false_type
{ };

template<typename Result>
struct is_callable_impl<Result, void_t<typename Result::type>> : std::true_type
{ };

#if __cplusplus >= 201703L

template<typename Functor>
using is_callable = std::is_invocable<Functor>;

#else // C++17

template<typename Functor>
struct is_callable : is_callable_impl<std::result_of<Functor&&()>>
{
  static_assert(is_complete_or_unbounded(type_identity<Functor>{}),
                "Functor must be a complete class or an unbounded array");
};

#endif // C++14

template <typename Functor, std::enable_if_t<is_callable<Functor>::value, bool> = true>
class Callable
{
public:
  using result_type = invoke_result_t<Functor>;

  Callable(const Functor &f) : f_(f)
  { }

  Callable(Functor &&f) : f_(std::move(f)) {}

  result_type call()
  {
    return f_();
  }

private:
  Functor f_;
};

/// \interface Task
/// \brief Task Interface
/// \headerfile "cxxthreads/task.h"
struct Task
{
  virtual ~Task() = default;

  /// Inherited by subclass.
  virtual void operator()() = 0;
};

template <typename Functor>
class RunnableTask : public Task
{
public:
  using result_type = void;

  RunnableTask(const Functor &f) : callable_(f) {}

  RunnableTask(Functor &&f) : callable_(std::move(f)) {}

  RunnableTask(const RunnableTask &) = delete;

  RunnableTask &operator=(const RunnableTask &) = delete;

  virtual ~RunnableTask() = default;

  virtual void operator()() override
  {
    callable_.call();
  }

private:
  Callable<Functor> callable_;
};

template <typename Functor>
class CallableTask : public Task
{
public:
  using result_type = typename Callable<Functor>::result_type;

  CallableTask(const Functor &f) : callable_(f) {}

  CallableTask(Functor &&f) : callable_(std::move(f)) {}

  CallableTask(const CallableTask &) = delete;

  CallableTask &operator=(const CallableTask &) = delete;

  virtual ~CallableTask() = default;

  std::future<result_type> getFuture()
  {
    return result_.get_future();
  }

  virtual void operator()() override
  {
    callAndSetValue<result_type>();
  }

private:
  // result_type is not void
  template <typename Ret, std::enable_if_t<!std::is_same<Ret, void>::value, bool> = true>
  void callAndSetValue()
  {
    result_.set_value(callable_.call());
  }

  // result_type is void
  template <typename Ret, std::enable_if_t<std::is_same<Ret, void>::value, bool> = true>
  void callAndSetValue()
  {
    callable_.call();
    result_.set_value();
  }

  Callable<Functor> callable_;
  std::promise<result_type> result_;
};

template <template <typename> class Task, typename Func, typename... Args>
decltype(auto) createTask(Func &&f, Args &&...args)
{
  auto bind_result = std::bind(std::forward<Func>(f), std::forward<Args>(args)...);

  using func_type = std::decay_t<decltype(bind_result)>;
  return std::make_shared<Task<func_type>>(std::move(bind_result));
}

} // namespace cxxthreads

#endif // CXXTHREADS_TASK_H