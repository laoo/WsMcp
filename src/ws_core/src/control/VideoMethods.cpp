#include "Methods.hpp"

#include <miniz.h>

#include <filesystem>
#include <fstream>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace ws::control
{

namespace
{

constexpr std::uint64_t MAX_SCALE = 8;

/// RGBA pixels, `width` by `height`, as a PNG of RGB pixels each `scale` by
/// `scale`.
std::vector<std::uint8_t>
encodePng( std::span<std::uint8_t const> rgba, std::size_t width, std::size_t height, std::size_t scale )
{
  std::vector<std::uint8_t> rgb;
  rgb.reserve( width * height * scale * scale * 3 );
  for ( std::size_t y = 0; y < height * scale; ++y )
  {
    std::size_t const row = ( y / scale ) * width;
    for ( std::size_t x = 0; x < width * scale; ++x )
    {
      std::size_t const at = ( row + ( x / scale ) ) * 4;
      rgb.insert( rgb.end(), { rgba[at], rgba[at + 1], rgba[at + 2] } );
    }
  }
  width *= scale;
  height *= scale;
  std::size_t size = 0;
  void* const png = tdefl_write_image_to_png_file_in_memory_ex(
      rgb.data(), static_cast<int>( width ), static_cast<int>( height ), 3, &size, MZ_DEFAULT_LEVEL, 0 );
  if ( png == nullptr )
  {
    return {};
  }
  std::vector<std::uint8_t> bytes( static_cast<std::uint8_t*>( png ), static_cast<std::uint8_t*>( png ) + size );
  mz_free( png );
  return bytes;
}

std::string base64( std::span<std::uint8_t const> bytes )
{
  static constexpr std::string_view ALPHABET = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string text;
  text.reserve( ( ( bytes.size() + 2 ) / 3 ) * 4 );
  for ( std::size_t at = 0; at < bytes.size(); at += 3 )
  {
    std::uint32_t const chunk = ( static_cast<std::uint32_t>( bytes[at] ) << 16U ) |
                                ( at + 1 < bytes.size() ? static_cast<std::uint32_t>( bytes[at + 1] ) << 8U : 0U ) |
                                ( at + 2 < bytes.size() ? bytes[at + 2] : 0U );
    text.push_back( ALPHABET[( chunk >> 18U ) & 63U] );
    text.push_back( ALPHABET[( chunk >> 12U ) & 63U] );
    text.push_back( at + 1 < bytes.size() ? ALPHABET[( chunk >> 6U ) & 63U] : '=' );
    text.push_back( at + 2 < bytes.size() ? ALPHABET[chunk & 63U] : '=' );
  }
  return text;
}

Error screenshotFailed( std::string message )
{
  return Error{ .code = "screenshot_failed", .message = std::move( message ) };
}

} // namespace

void addVideoMethods( Dispatcher& dispatcher, Emulator& emulator )
{
  dispatcher.add(
      "video.screenshot",
      info( "The last complete picture: the LCD's 224 by 144 dots (with the LCD's icons beside or below them when "
            "the emulator shows them), in its own grey shades on a WonderSwan and its colours on a Color. A "
            "vertical game draws on its side, as the LCD is held turned; vertical says so. Written to path as a "
            "PNG, or returned as one.",
            { { .name = "path",
                .type = "string",
                .description = "Where to write the PNG; left out, it is returned.",
                .required = false },
              { .name = "scale",
                .type = "integer",
                .description = "Each dot as scale by scale pixels, 1 to 8; 1 when left out.",
                .required = false } } ),
      [&emulator]( Json const& params ) -> Outcome
      {
        auto const machine = machineOf( emulator );
        if ( !machine )
        {
          return std::unexpected( machine.error() );
        }
        std::uint64_t scale = 1;
        if ( params.contains( "scale" ) )
        {
          auto const given = requireUnsigned( params, "scale" );
          if ( !given || *given == 0 || *given > MAX_SCALE )
          {
            return std::unexpected( badRequest( "scale is 1 to 8" ) );
          }
          scale = *given;
        }
        std::optional<std::string> path;
        if ( params.contains( "path" ) )
        {
          auto const given = requireString( params, "path" );
          if ( !given )
          {
            return std::unexpected( given.error() );
          }
          path = *given;
        }
        machine::Machine const& ws = **machine;
        std::size_t const width = ws.pictureWidth();
        std::size_t const height = ws.pictureHeight();
        if ( ws.picture().size() < width * height * 4 )
        {
          return std::unexpected( screenshotFailed( "The picture is smaller than its size says" ) );
        }
        auto const png = encodePng( ws.picture(), width, height, static_cast<std::size_t>( scale ) );
        if ( png.empty() )
        {
          return std::unexpected( screenshotFailed( "The picture could not be encoded" ) );
        }
        Json result{ { "width", width * scale },
                     { "height", height * scale },
                     { "scale", scale },
                     { "frame", ws.picturesDrawn() },
                     { "vertical", ws.vertical() } };
        if ( path )
        {
          std::ofstream file{ std::filesystem::path{ *path }, std::ios::binary };
          file.write( reinterpret_cast<char const*>( png.data() ), static_cast<std::streamsize>( png.size() ) );
          if ( !file )
          {
            return std::unexpected( screenshotFailed( "Cannot write " + *path ) );
          }
          result["path"] = *path;
          return result;
        }
        result["png_base64"] = base64( png );
        return result;
      } );
}

} // namespace ws::control
