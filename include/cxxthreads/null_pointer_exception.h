//=========-- cxxthreads/null_pointer_exception.h ---------------*- C++ -*-===//
//
//  Under Apache License v2.0.
//  Author: Patrick
//
//===----------------------------------------------------------------------===//
//
// This file defines the NullPointerException class. 
//
//===----------------------------------------------------------------------===//

#ifndef CXXTHREADS_NULLPOINTEREXCEPTION_H
#define CXXTHREADS_NULLPOINTEREXCEPTION_H

#include <stdexcept>

namespace cxxthreads
{

/// Thrown when an application attempts to use nullptr in a
/// case where an object is required.
/// \headerfile "cxxthreads/null_pointer_exception.h"
class NullPointerException : public std::runtime_error
{
public:
  /// \param what_arg explanatory string
  explicit NullPointerException(const std::string &what_arg) : std::runtime_error(what_arg)
  {}

  /// \param what_arg explanatory string
  explicit NullPointerException(const char *what_arg) : std::runtime_error(what_arg)
  {}
};

} // namespace cxxthreads

#endif // CXXTHREADS_NULLPOINTEREXCEPTION_H