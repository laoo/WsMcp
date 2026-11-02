#pragma once

#include <expected>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

namespace ws::test
{

/// The value of `result`, or a failure of the test case carrying its error.
///
/// Catch2's REQUIRE stops a test that fails, but clang-tidy cannot see that it
/// does, and so takes every access after `REQUIRE( x.has_value() )` for an
/// unchecked one. Here the check is plain code, and the error the result holds
/// -- why a file was refused, say -- becomes the failure's message.
template <class T, class E>
T valueOf( std::expected<T, E> result )
{
  if ( !result.has_value() )
  {
    if constexpr ( std::is_convertible_v<E, std::string> )
    {
      throw std::runtime_error( "expected a value, got the error: " + std::string{ result.error() } );
    }
    else
    {
      throw std::runtime_error( "expected a value, got an error" );
    }
  }
  return std::move( result ).value();
}

/// The value of `optional`, or a failure of the test case.
template <class T>
T valueOf( std::optional<T> optional )
{
  if ( !optional.has_value() )
  {
    throw std::runtime_error( "expected a value, got none" );
  }
  return std::move( optional ).value();
}

} // namespace ws::test
