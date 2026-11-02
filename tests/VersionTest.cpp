#include <catch2/catch_test_macros.hpp>

#include "ws/Version.hpp"

#include <cctype>
#include <string_view>

// The version is `git describe`'s answer or `devel`, so its exact shape is not
// this suite's to pin down. What is worth holding is that CMake resolved
// something at all: every way this can go wrong ends in an empty or a ragged
// definition, and `--version` would then print it without complaint.
TEST_CASE( "the version is the tag the build was made from, or devel", "[version]" )
{
  auto const version = ws::versionString();

  REQUIRE_FALSE( version.empty() );
  REQUIRE( version.find_first_of( " \t\r\n\"" ) == std::string_view::npos );
  REQUIRE( ( version.starts_with( "devel" ) || std::isdigit( static_cast<unsigned char>( version.front() ) ) != 0 ) );
}
