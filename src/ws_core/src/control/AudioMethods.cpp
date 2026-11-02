#include "Methods.hpp"

#include <spdlog/fmt/fmt.h>

#include <filesystem>
#include <fstream>
#include <vector>

namespace ws::control
{

namespace
{

/// `frames` as a WAV file: 16-bit stereo PCM at the machine's rate.
bool writeWav( std::filesystem::path const& path, std::vector<machine::AudioFrame> const& frames )
{
  std::ofstream file{ path, std::ios::binary };
  auto const put32 = [&file]( std::uint32_t value )
  {
    for ( unsigned shift = 0; shift < 32; shift += 8 )
    {
      file.put( static_cast<char>( ( value >> shift ) & 0xffU ) );
    }
  };
  auto const put16 = [&file]( std::uint16_t value )
  {
    file.put( static_cast<char>( value & 0xffU ) );
    file.put( static_cast<char>( value >> 8U ) );
  };
  constexpr std::uint32_t bytesPerFrame = 4;
  auto const dataSize = static_cast<std::uint32_t>( frames.size() * bytesPerFrame );
  file.write( "RIFF", 4 );
  put32( 36 + dataSize );
  file.write( "WAVEfmt ", 8 );
  put32( 16 );
  put16( 1 );
  put16( 2 );
  put32( machine::Machine::AUDIO_RATE );
  put32( machine::Machine::AUDIO_RATE * bytesPerFrame );
  put16( bytesPerFrame );
  put16( 16 );
  file.write( "data", 4 );
  put32( dataSize );
  for ( machine::AudioFrame const& frame : frames )
  {
    put16( static_cast<std::uint16_t>( frame.left ) );
    put16( static_cast<std::uint16_t>( frame.right ) );
  }
  return static_cast<bool>( file );
}

} // namespace

void addAudioMethods( Dispatcher& dispatcher, Emulator& emulator )
{
  dispatcher.add(
      "audio.capture_start",
      info( "Keeps the machine's sound from now on, every run's, until audio.capture_stop writes it: stereo at 24 "
            "kHz, the four channels and Hyper Voice mixed as the headphones hear them (or the speaker, as the "
            "emulator is set), 16 bits." ),
      [&emulator]( Json const& /*params*/ ) -> Outcome
      {
        auto const machine = machineOf( emulator );
        if ( !machine )
        {
          return std::unexpected( machine.error() );
        }
        if ( ( *machine )->capturing() )
        {
          return std::unexpected( Error{ .code = "capture_running", .message = "A capture is running already" } );
        }
        ( *machine )->startCapture();
        return Json::object();
      } );

  dispatcher.add( "audio.capture_stop",
                  info( "Ends the capture, and writes what it kept to path as a WAV: 16-bit stereo at 24 kHz.",
                        { { .name = "path", .type = "string", .description = "Where to write the WAV." } } ),
                  [&emulator]( Json const& params ) -> Outcome
                  {
                    auto const machine = machineOf( emulator );
                    if ( !machine )
                    {
                      return std::unexpected( machine.error() );
                    }
                    auto const path = requireString( params, "path" );
                    if ( !path )
                    {
                      return std::unexpected( path.error() );
                    }
                    if ( !( *machine )->capturing() )
                    {
                      return std::unexpected(
                          Error{ .code = "capture_not_running", .message = "No capture is running" } );
                    }
                    std::vector<machine::AudioFrame> const frames = ( *machine )->stopCapture();
                    if ( !writeWav( *path, frames ) )
                    {
                      return std::unexpected(
                          Error{ .code = "capture_failed", .message = fmt::format( "Cannot write {}", *path ) } );
                    }
                    return Json{ { "path", *path },
                                 { "frames", frames.size() },
                                 { "seconds", static_cast<double>( frames.size() ) / machine::Machine::AUDIO_RATE } };
                  } );
}

} // namespace ws::control
