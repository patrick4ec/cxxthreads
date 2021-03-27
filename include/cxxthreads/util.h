//===---------------------- cxxthreads/util.h -------------------*- C++ -*-===//
//
//  Under Apache License v2.0.
//  Author: Patrick
//
//===----------------------------------------------------------------------===//
//
// This file defines some utility classes. 
//
//===----------------------------------------------------------------------===//

#ifndef CXXTHREADS_UTIL_H
#define CXXTHREADS_UTIL_H

#include <type_traits>

namespace cxxthreads 
{

#if __cplusplus >= 201703L

template<class F, class... ArgTypes >
using invoke_result_t = typename std::invoke_result<F, ArgTypes...>::type;

#else // C++17

template<class F, class... ArgTypes >
using invoke_result_t = typename std::result_of<F&&(ArgTypes &&...)>::type;

#endif // C++14

} // namespace cxxthreads

#endif // CXXTHREADS_UTIL_H