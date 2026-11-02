#include "ws/Emulator.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <exception>
#include <fstream>
#include <iterator>
#include <system_error>
#include <utility>

namespace ws
{

namespace
{

/// The smallest cartridge: one bank of 64 KB, its header at its end.
constexpr std::uintmax_t MIN_ROM_SIZE = 0x10000;
/// The largest a mapper reaches: 256 banks of 64 KB.
constexpr std::uintmax_t MAX_ROM_SIZE = 0x1000000;

/// A clock's file: its registers, then the host's time.
constexpr std::size_t RTC_REGISTERS = 10;
constexpr std::size_t RTC_FILE_SIZE = RTC_REGISTERS + 8;

/// The host's time, seconds since 1970: what a battery-backed clock follows
/// while the emulator is off.
std::uint64_t hostSeconds()
{
  auto const since = std::chrono::system_clock::now().time_since_epoch();
  return static_cast<std::uint64_t>(
      std::max<std::int64_t>( 0, std::chrono::duration_cast<std::chrono::seconds>( since ).count() ) );
}

std::expected<std::vector<std::uint8_t>, std::string> readFile( std::filesystem::path const& path,
                                                                std::uintmax_t maxSize )
{
  std::error_code error;
  std::uintmax_t const size = std::filesystem::file_size( path, error );
  if ( error )
  {
    return std::unexpected( "cannot read " + path.string() + ": " + error.message() );
  }
  if ( size > maxSize )
  {
    return std::unexpected( path.string() + " is too large" );
  }
  std::ifstream file{ path, std::ios::binary };
  std::vector<std::uint8_t> bytes( static_cast<std::size_t>( size ) );
  file.read( reinterpret_cast<char*>( bytes.data() ), static_cast<std::streamsize>( size ) );
  if ( !file )
  {
    return std::unexpected( "cannot read " + path.string() );
  }
  return bytes;
}

std::expected<void, std::string> writeFile( std::filesystem::path const& path, std::span<std::uint8_t const> bytes )
{
  std::ofstream file{ path, std::ios::binary | std::ios::trunc };
  file.write( reinterpret_cast<char const*>( bytes.data() ), static_cast<std::streamsize>( bytes.size() ) );
  if ( !file )
  {
    return std::unexpected( "cannot write " + path.string() );
  }
  return {};
}

/// The names a boot ROM goes by: Mesen2's, then No-Intro's.
std::array<std::string_view, 2> bootRomNames( machine::Model model )
{
  switch ( model )
  {
  case machine::Model::MONOCHROME:
    return { "bootrom.ws", "[BIOS] WonderSwan Boot ROM (Japan) (En).ws" };
  case machine::Model::COLOR:
    return { "bootrom.wsc", "[BIOS] WonderSwan Color Boot ROM (Japan) (En).wsc" };
  case machine::Model::SWAN_CRYSTAL:
    return { "bootrom_sc.wsc", "[BIOS] SwanCrystal Boot ROM (Japan) (En).wsc" };
  }
  return {};
}

std::size_t bootRomSize( machine::Model model )
{
  return model == machine::Model::MONOCHROME ? 0x1000 : 0x2000;
}

} // namespace

char const* modelName( machine::Model model )
{
  switch ( model )
  {
  case machine::Model::MONOCHROME:
    return "ws";
  case machine::Model::COLOR:
    return "wsc";
  case machine::Model::SWAN_CRYSTAL:
    return "sc";
  }
  return "";
}

std::optional<machine::Model> modelFromName( std::string_view name )
{
  for ( machine::Model const model :
        { machine::Model::MONOCHROME, machine::Model::COLOR, machine::Model::SWAN_CRYSTAL } )
  {
    if ( name == modelName( model ) )
    {
      return model;
    }
  }
  return std::nullopt;
}

Emulator::Emulator( Settings settings ) : mSettings{ std::move( settings ) }
{
  if ( auto const powered = powerUp(); !powered )
  {
    mPowerUpError = powered.error();
  }
}

Emulator::~Emulator()
{
  // A save that cannot be written now can only be told about.
  try
  {
    if ( auto const flushed = flushSaves(); !flushed )
    {
      spdlog::error( "The saves were not written: {}", flushed.error() );
    }
  }
  catch ( std::exception const& error )
  {
    // Nothing may escape a destructor; the log is the last word.
    std::fputs( error.what(), stderr );
  }
}

std::optional<std::filesystem::path> Emulator::bootRomPath( machine::Model model ) const
{
  if ( mSettings.biosDirectory.empty() )
  {
    return std::nullopt;
  }
  for ( std::string_view const name : bootRomNames( model ) )
  {
    std::filesystem::path path = mSettings.biosDirectory / name;
    std::error_code error;
    if ( std::filesystem::file_size( path, error ) == bootRomSize( model ) && !error )
    {
      return path;
    }
  }
  return std::nullopt;
}

std::filesystem::path Emulator::savePath() const
{
  return mSettings.saveDirectory / mRunningCartridge.filename().replace_extension( ".sav" );
}

std::filesystem::path Emulator::rtcPath() const
{
  return mSettings.saveDirectory / mRunningCartridge.filename().replace_extension( ".rtc" );
}

std::filesystem::path Emulator::flashPath() const
{
  return mSettings.saveDirectory / mRunningCartridge.filename().replace_extension( ".flash" );
}

std::filesystem::path Emulator::internalEepromPath( machine::Model model ) const
{
  return mSettings.saveDirectory / ( std::string{ modelName( model ) } + ".ieeprom" );
}

std::expected<void, std::string> Emulator::powerUp()
{
  if ( mSettings.cartridge.empty() )
  {
    return std::unexpected( std::string{ "no cartridge is in" } );
  }
  auto rom = readFile( mSettings.cartridge, MAX_ROM_SIZE );
  if ( !rom )
  {
    return std::unexpected( rom.error() );
  }
  if ( rom->size() < MIN_ROM_SIZE )
  {
    return std::unexpected( mSettings.cartridge.string() + " is smaller than 64 KB: not a WonderSwan cartridge" );
  }

  if ( auto const flushed = flushSaves(); !flushed )
  {
    return std::unexpected( flushed.error() );
  }

  machine::Model model = machine::Model::MONOCHROME;
  if ( mSettings.model )
  {
    model = *mSettings.model;
  }
  else if ( ( ( *rom )[rom->size() - 9] & 0x01U ) != 0 || mSettings.cartridge.extension() == ".wsc" )
  {
    model = machine::Model::COLOR;
  }

  machine::Setup setup{ .model = model,
                        .rom = std::move( *rom ),
                        .bootRom = {},
                        .internalEeprom = {},
                        .save = {},
                        .rtc = {},
                        .rtcSecondsAway = 0,
                        .flash = {},
                        .audioMode = mSettings.audioMode,
                        .lcdShowIcons = mSettings.lcdShowIcons };
  if ( mSettings.useBootRom )
  {
    if ( auto const path = bootRomPath( model ) )
    {
      auto bootRom = readFile( *path, bootRomSize( model ) );
      if ( !bootRom )
      {
        return std::unexpected( bootRom.error() );
      }
      setup.bootRom = std::move( *bootRom );
    }
  }
  bool const bootRomUsed = !setup.bootRom.empty();

  mRunningCartridge = mSettings.cartridge;
  if ( !mSettings.saveDirectory.empty() )
  {
    // A save that is not there yet is a blank one.
    if ( auto save = readFile( savePath(), MAX_ROM_SIZE ) )
    {
      setup.save = std::move( *save );
    }
    if ( auto eeprom = readFile( internalEepromPath( model ), 0x800 ) )
    {
      setup.internalEeprom = std::move( *eeprom );
    }
    // A flash chip written before is as it was left; a file of another
    // size is not this cartridge's, and is left out.
    if ( auto flash = readFile( flashPath(), MAX_ROM_SIZE ) )
    {
      setup.flash = std::move( *flash );
    }
    // The clock ran on its battery while the emulator was off.
    if ( auto clock = readFile( rtcPath(), RTC_FILE_SIZE ); clock && clock->size() == RTC_FILE_SIZE )
    {
      std::uint64_t saved = 0;
      for ( std::size_t i = 0; i < 8; ++i )
      {
        saved |= std::uint64_t{ ( *clock )[RTC_REGISTERS + i] } << ( 8 * i );
      }
      std::uint64_t const now = hostSeconds();
      setup.rtc.assign( clock->begin(), clock->begin() + RTC_REGISTERS );
      setup.rtcSecondsAway = now > saved ? now - saved : 0;
    }
  }

  mMachine = std::make_unique<machine::Machine>( std::move( setup ) );
  mMachine->setTraceCycles( mTraceCycles );
  mBootRomUsed = bootRomUsed;
  ++mPowerUps;
  mPowerUpError.clear();
  return {};
}

std::expected<void, std::string> Emulator::flushSaves()
{
  if ( !mMachine || mSettings.saveDirectory.empty() )
  {
    return {};
  }
  std::error_code error;
  std::filesystem::create_directories( mSettings.saveDirectory, error );
  for ( machine::Memory const which : { machine::Memory::SAVE_RAM, machine::Memory::CART_EEPROM } )
  {
    std::span<std::uint8_t const> const memory = std::as_const( *mMachine ).memory( which );
    if ( !memory.empty() )
    {
      if ( auto const written = writeFile( savePath(), memory ); !written )
      {
        return written;
      }
    }
  }
  if ( mMachine->flashWritten() )
  {
    if ( auto const written = writeFile( flashPath(), std::as_const( *mMachine ).memory( machine::Memory::ROM ) );
         !written )
    {
      return written;
    }
  }
  if ( mMachine->hasRtc() )
  {
    std::vector<std::uint8_t> clock = mMachine->rtc();
    std::uint64_t const now = hostSeconds();
    for ( std::size_t i = 0; i < 8; ++i )
    {
      clock.push_back( static_cast<std::uint8_t>( now >> ( 8 * i ) ) );
    }
    if ( auto const written = writeFile( rtcPath(), clock ); !written )
    {
      return written;
    }
  }
  return writeFile( internalEepromPath( mMachine->model() ),
                    std::as_const( *mMachine ).memory( machine::Memory::INTERNAL_EEPROM ) );
}

std::expected<void, std::string> Emulator::loadCartridge( std::filesystem::path const& path,
                                                          std::optional<machine::Model> model )
{
  Settings const previous = mSettings;
  mSettings.cartridge = path;
  mSettings.model = model;
  auto powered = powerUp();
  if ( !powered )
  {
    mSettings = previous;
  }
  return powered;
}

std::expected<void, std::string> Emulator::setBiosDirectory( std::filesystem::path const& directory )
{
  if ( mSettings.cartridge.empty() )
  {
    // Nothing to power up yet: the next cartridge starts with these.
    mSettings.biosDirectory = directory;
    return {};
  }
  std::filesystem::path const previous = std::exchange( mSettings.biosDirectory, directory );
  auto powered = powerUp();
  if ( !powered )
  {
    mSettings.biosDirectory = previous;
  }
  return powered;
}

machine::Machine* Emulator::machine()
{
  return mMachine.get();
}

machine::Machine const* Emulator::machine() const
{
  return mMachine.get();
}

bool Emulator::bootRomUsed() const
{
  return mBootRomUsed;
}

void Emulator::setTraceCycles( std::uint64_t cycles )
{
  mTraceCycles = cycles;
  if ( mMachine )
  {
    mMachine->setTraceCycles( cycles );
  }
}

std::uint64_t Emulator::traceCycles() const
{
  return mTraceCycles;
}

std::uint64_t Emulator::powerUps() const
{
  return mPowerUps;
}

std::string const& Emulator::powerUpError() const
{
  return mPowerUpError;
}

Settings const& Emulator::settings() const
{
  return mSettings;
}

bool Emulator::paused() const
{
  return mPaused;
}

void Emulator::setPaused( bool paused )
{
  mPaused = paused;
}

} // namespace ws
