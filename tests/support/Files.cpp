#include "support/Files.hpp"

#include <miniz.h>

#include <atomic>
#include <chrono>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <system_error>

namespace ws::test
{

TemporaryDirectory::TemporaryDirectory()
{
  // The clock and a counter, so that two tests running at once, or one test
  // run twice in a row, never share a directory.
  static std::atomic<unsigned> counter{ 0 };
  auto const stamp = std::chrono::steady_clock::now().time_since_epoch().count();
  mPath = std::filesystem::temp_directory_path() /
          ( "wsmcp-test-" + std::to_string( stamp ) + "-" + std::to_string( counter++ ) );
  std::filesystem::create_directories( mPath );
}

TemporaryDirectory::~TemporaryDirectory()
{
  std::error_code ignored;
  std::filesystem::remove_all( mPath, ignored );
}

std::filesystem::path const& TemporaryDirectory::path() const
{
  return mPath;
}

void writeFile( std::filesystem::path const& path, std::vector<std::uint8_t> const& bytes )
{
  std::ofstream stream{ path, std::ios::binary };
  stream.write( reinterpret_cast<char const*>( bytes.data() ), static_cast<std::streamsize>( bytes.size() ) );
  if ( !stream )
  {
    throw std::runtime_error( "cannot write " + path.string() );
  }
}

void writeZip( std::filesystem::path const& path,
               std::vector<std::pair<std::string, std::vector<std::uint8_t>>> const& files )
{
  mz_zip_archive zip{};
  if ( mz_zip_writer_init_file( &zip, path.string().c_str(), 0 ) == 0 )
  {
    throw std::runtime_error( "cannot create " + path.string() );
  }
  for ( auto const& [name, bytes] : files )
  {
    mz_zip_writer_add_mem( &zip, name.c_str(), bytes.data(), bytes.size(), static_cast<mz_uint>( MZ_DEFAULT_LEVEL ) );
  }
  bool const finished = mz_zip_writer_finalize_archive( &zip ) != 0;
  mz_zip_writer_end( &zip );
  if ( !finished )
  {
    throw std::runtime_error( "cannot finish " + path.string() );
  }
}

std::string readGzip( std::filesystem::path const& path )
{
  std::ifstream stream{ path, std::ios::binary };
  std::vector<std::uint8_t> const packed{ std::istreambuf_iterator<char>{ stream }, std::istreambuf_iterator<char>{} };
  if ( packed.size() <= 18 || packed[0] != 0x1f || packed[1] != 0x8b )
  {
    throw std::runtime_error{ "not a gzip file: " + path.string() };
  }

  // RFC 1952: a 10-byte header, then the optional fields its flags announce,
  // then raw deflate.
  std::uint8_t const flags = packed[3];
  std::size_t at = 10;
  if ( ( flags & 0x04U ) != 0 )
  {
    at += 2 + static_cast<std::size_t>( packed[at] | ( packed[at + 1] << 8U ) );
  }
  for ( std::uint8_t const terminated : { std::uint8_t{ 0x08 }, std::uint8_t{ 0x10 } } )
  {
    if ( ( flags & terminated ) != 0 )
    {
      while ( packed[at] != 0 )
      {
        ++at;
      }
      ++at;
    }
  }
  if ( ( flags & 0x02U ) != 0 )
  {
    at += 2;
  }

  std::size_t size = 0;
  void* const text = tinfl_decompress_mem_to_heap( packed.data() + at, packed.size() - at, &size, 0 );
  if ( text == nullptr )
  {
    throw std::runtime_error{ "cannot inflate " + path.string() };
  }
  std::string result{ static_cast<char const*>( text ), size };
  mz_free( text );
  return result;
}

} // namespace ws::test
