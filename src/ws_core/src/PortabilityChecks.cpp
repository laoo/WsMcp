// WsMcp targets C++23 across Apple clang, GCC and MSVC, but only the subset all
// three implement. These assertions fail the build the moment someone reaches
// for a feature that is missing on one of the three platforms, instead of
// letting CI discover it later. Taken from NGA, which found the hard way where
// the three disagree.

#include <array>
#include <bit>
#include <expected>
#include <filesystem>
#include <memory>
#include <span>
#include <utility>
#include <vector>
#include <version>

static_assert( __cpp_lib_expected >= 202202L, "std::expected is required" );
static_assert( __cpp_lib_span >= 202002L, "std::span is required" );
static_assert( __cpp_lib_filesystem >= 201703L, "std::filesystem is required" );
static_assert( __cpp_lib_bit_cast >= 201806L, "std::bit_cast is required" );
static_assert( __cpp_lib_byteswap >= 202110L, "std::byteswap is required" );
static_assert( __cpp_lib_to_underlying >= 202102L, "std::to_underlying is required" );
static_assert( __cpp_lib_string_contains >= 202011L, "std::string::contains is required" );
static_assert( __cpp_explicit_this_parameter >= 202110L, "deducing this is required" );

// Two differences between the three standard libraries that no feature macro
// reports, both found by MSVC after the other two had accepted the code.

// A container of a move-only type still *declares* a copy constructor, which
// only fails when it is instantiated. A type holding one therefore looks
// copy-constructible, and a container whose move is not noexcept -- true of
// MSVC's unordered_map -- reaches for that copy and fails deep inside the
// standard library. Owning types say `= delete` rather than leaving it to be
// derived.
static_assert( std::is_copy_constructible_v<std::vector<std::unique_ptr<int>>>,
               "a container of a move-only type still declares a copy constructor" );

// A standard container's iterator is a pointer on libc++ and a class type on
// MSVC, so `auto*` deduces from one and not from the other. Nothing can be
// asserted either way, since both are conforming -- which is exactly why it is
// written down: never deduce a raw pointer from an iterator. It is `auto const`
// at every such site, and readability-qualified-auto, which asks for the other
// spelling, is off in `.clang-tidy` for that reason.
