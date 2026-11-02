#include "ws/control/Batch.hpp"

#include <miniz.h>
#include <spdlog/fmt/fmt.h>

#include <algorithm>
#include <array>
#include <memory>
#include <span>
#include <utility>

namespace ws::control
{

namespace
{

std::string crcText( mz_ulong crc )
{
  return fmt::format( "{:08x}", static_cast<std::uint32_t>( crc ) );
}

std::string crcOf( std::span<std::uint8_t const> bytes )
{
  return crcText( mz_crc32( MZ_CRC32_INIT, bytes.data(), bytes.size() ) );
}

/// The error codes that say a step needs something that is not there: a
/// cartridge, or a file it names.
bool isMissing( std::string const& code )
{
  return code == "not_found" || code == "no_machine";
}

/// The sound a script's machine makes, summed as it comes.
struct SoundSum
{
  machine::Machine* listenedTo{};
  mz_ulong crc{ MZ_CRC32_INIT };

  void add( std::span<machine::AudioFrame const> frames )
  {
    for ( machine::AudioFrame const& frame : frames )
    {
      std::array<std::uint8_t, 4> bytes{};
      for ( std::size_t i = 0; i < 2; ++i )
      {
        auto const sample = static_cast<std::uint16_t>( i == 0 ? frame.left : frame.right );
        bytes.at( i * 2 ) = static_cast<std::uint8_t>( sample );
        bytes.at( ( i * 2 ) + 1 ) = static_cast<std::uint8_t>( sample >> 8U );
      }
      crc = mz_crc32( crc, bytes.data(), bytes.size() );
    }
  }
};

BatchResult failed( BatchResult result, BatchResult::Outcome outcome, std::string message )
{
  result.outcome = outcome;
  result.message = std::move( message );
  return result;
}

Json sumsOf( machine::Machine const& machine, SoundSum const& sound )
{
  return Json{ { "frame", machine.frame() },
               { "picture", crcOf( machine.picture() ) },
               { "work_ram", crcOf( machine.memory( machine::Memory::WORK_RAM ) ) },
               { "sound", crcText( sound.crc ) } };
}

} // namespace

BatchResult runBatch( Settings settings, Json const& script )
{
  BatchResult result;
  if ( !script.is_object() || !script.contains( "steps" ) || !script.at( "steps" ).is_array() )
  {
    return failed( std::move( result ), BatchResult::Outcome::FAILED, "A script is an object with steps" );
  }

  if ( script.contains( "boot_rom" ) && !script.at( "boot_rom" ).is_boolean() )
  {
    return failed( std::move( result ), BatchResult::Outcome::FAILED, "A script's boot_rom is true or false" );
  }
  bool const needsBootRom = script.value( "boot_rom", false );

  // A machine runs from the start when the settings name a cartridge; a
  // script may put one in with cart.load instead.
  Emulator emulator{ std::move( settings ) };
  Dispatcher const dispatcher{ emulator };
  auto sound = std::make_shared<SoundSum>();
  auto const listen = [&emulator, &sound]
  {
    // A power-up is a machine of its own; its sound is summed from then on.
    machine::Machine* const machine = emulator.machine();
    if ( machine != nullptr && machine != sound->listenedTo )
    {
      machine->setAudioListener( [sound]( std::span<machine::AudioFrame const> frames ) { sound->add( frames ); } );
      sound->listenedTo = machine;
      sound->crc = MZ_CRC32_INIT;
    }
  };
  listen();

  std::size_t index = 0;
  for ( Json const& step : script.at( "steps" ) )
  {
    ++index;
    if ( step.contains( "checkpoint" ) )
    {
      machine::Machine const* const machine = emulator.machine();
      if ( machine == nullptr )
      {
        return failed(
            std::move( result ),
            BatchResult::Outcome::MISSING,
            fmt::format( "step {}: a checkpoint with no machine running: {}", index, emulator.powerUpError() ) );
      }
      if ( needsBootRom && !emulator.bootRomUsed() )
      {
        return failed( std::move( result ),
                       BatchResult::Outcome::MISSING,
                       fmt::format( "step {}: the script runs the boot ROM, and the BIOS directory has no {} one",
                                    index,
                                    modelName( machine->model() ) ) );
      }
      result.checkpoints.push_back(
          Checkpoint{ .name = step.at( "checkpoint" ).get<std::string>(), .sums = sumsOf( *machine, *sound ) } );
      sound->crc = MZ_CRC32_INIT;
      continue;
    }

    if ( !step.contains( "method" ) )
    {
      return failed( std::move( result ),
                     BatchResult::Outcome::FAILED,
                     fmt::format( "step {}: neither a request nor a checkpoint", index ) );
    }
    Json request{ { "id", index }, { "method", step.at( "method" ) } };
    if ( step.contains( "params" ) )
    {
      request["params"] = step.at( "params" );
    }
    Json const response = dispatcher.handle( request );
    if ( response.at( "ok" ) != true )
    {
      auto const& error = response.at( "error" );
      auto const code = error.at( "code" ).get<std::string>();
      return failed( std::move( result ),
                     isMissing( code ) ? BatchResult::Outcome::MISSING : BatchResult::Outcome::FAILED,
                     fmt::format( "step {} ({}): {}: {}",
                                  index,
                                  step.at( "method" ).get<std::string>(),
                                  code,
                                  error.at( "message" ).get<std::string>() ) );
    }
    listen();
  }
  result.outcome = BatchResult::Outcome::RAN;
  return result;
}

std::vector<std::string> compareWithGolden( BatchResult const& result, Json const& golden )
{
  std::vector<std::string> differences;
  for ( Checkpoint const& checkpoint : result.checkpoints )
  {
    if ( !golden.is_object() || !golden.contains( checkpoint.name ) )
    {
      differences.push_back( fmt::format( "{}: has no golden sums", checkpoint.name ) );
      continue;
    }
    Json const& expected = golden.at( checkpoint.name );
    for ( auto const& [key, value] : checkpoint.sums.items() )
    {
      if ( !expected.contains( key ) || expected.at( key ) != value )
      {
        differences.push_back( fmt::format( "{}: {} is {}, golden {}",
                                            checkpoint.name,
                                            key,
                                            value.dump(),
                                            expected.contains( key ) ? expected.at( key ).dump() : "absent" ) );
      }
    }
  }
  if ( golden.is_object() )
  {
    for ( auto const& [name, sums] : golden.items() )
    {
      bool const reached = std::ranges::any_of(
          result.checkpoints, [&name]( Checkpoint const& checkpoint ) { return checkpoint.name == name; } );
      if ( !reached )
      {
        differences.push_back( fmt::format( "{}: not reached", name ) );
      }
    }
  }
  return differences;
}

Json goldenOf( BatchResult const& result )
{
  Json golden = Json::object();
  for ( Checkpoint const& checkpoint : result.checkpoints )
  {
    golden[checkpoint.name] = checkpoint.sums;
  }
  return golden;
}

} // namespace ws::control
