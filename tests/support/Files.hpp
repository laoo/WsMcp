#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace ws::test
{

/// A directory of its own under the system's temporary directory, removed with
/// everything in it when the object goes.
class TemporaryDirectory
{
public:
  TemporaryDirectory();
  ~TemporaryDirectory();

  TemporaryDirectory( TemporaryDirectory const& ) = delete;
  TemporaryDirectory& operator=( TemporaryDirectory const& ) = delete;
  TemporaryDirectory( TemporaryDirectory&& ) = delete;
  TemporaryDirectory& operator=( TemporaryDirectory&& ) = delete;

  [[nodiscard]] std::filesystem::path const& path() const;

private:
  std::filesystem::path mPath;
};

void writeFile( std::filesystem::path const& path, std::vector<std::uint8_t> const& bytes );

/// Writes a zip archive at `path` holding each (name, bytes) pair.
void writeZip( std::filesystem::path const& path,
               std::vector<std::pair<std::string, std::vector<std::uint8_t>>> const& files );

/// The content of a gzip file, such as the SingleStepTests suites' .json.gz.
std::string readGzip( std::filesystem::path const& path );

} // namespace ws::test
