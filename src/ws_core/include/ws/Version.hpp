#pragma once

#include <string_view>

namespace ws
{

/// Version of the emulator: the tag the build was made from, or `devel` where
/// there was no tag to describe. See cmake/Version.cmake.
std::string_view versionString();

} // namespace ws
