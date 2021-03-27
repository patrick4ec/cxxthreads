//===-- cxxthreads/rejected_execution_exception.h ---------------*- C++ -*-===//
//
//  Under Apache License v2.0.
//  Author: Patrick
//
//===----------------------------------------------------------------------===//
//
// This file defines the RejectedExecutionException class. 
//
//===----------------------------------------------------------------------===//

#ifndef CXXTHREADS_REJECTEDEXECUTIONEXCEPTION_H
#define CXXTHREADS_REJECTEDEXECUTIONEXCEPTION_H

#include <stdexcept>

namespace cxxthreads
{

/// \class RejectedExecutionException 
/// \brief Exception thrown by an ThreadPool when a task cannot be accepted for execution.
/// \headerfile "cxxthreads/rejected_execution_exception.h"
class RejectedExecutionException : public std::runtime_error
{
public:
  /// \param what_arg explanatory string
  explicit RejectedExecutionException(const std::string &what_arg) : std::runtime_error(what_arg)
  {}

  /// \param what_arg explanatory string
  explicit RejectedExecutionException(const char *what_arg) : std::runtime_error(what_arg)
  {}
};

} // namespace cxxthreads

#endif // CXXTHREADS_REJECTEDEXECUTIONEXCEPTION_H