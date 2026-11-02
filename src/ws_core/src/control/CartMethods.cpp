#include "Methods.hpp"

#include <spdlog/fmt/fmt.h>

#include <filesystem>
#include <optional>
#include <system_error>

namespace ws::control
{

namespace
{

/// The header: the last 16 bytes of the ROM.
constexpr std::size_t HEADER_SIZE = 16;
constexpr std::size_t GAME_VERSION = 9;
constexpr std::size_t CHECKSUM = 14;

/// What the cartridge in `machine` is, as cart.info and cart.load answer it.
Json cartJson( Emulator const& emulator, machine::Machine const& machine )
{
  std::span<std::uint8_t const> const rom = machine.memory( machine::Memory::ROM );
  Json info{ { "path", emulator.settings().cartridge.string() },
             { "model", modelName( machine.model() ) },
             { "publisher", machine.publisher() },
             { "game_id", machine.gameId() },
             { "color_support", machine.colorSupport() },
             { "vertical", machine.vertical() },
             { "rom_size", rom.size() },
             { "rom_crc32", crcText( machine.romCrc() ) } };
  if ( rom.size() >= HEADER_SIZE )
  {
    std::span<std::uint8_t const> const header = rom.last( HEADER_SIZE );
    // The checksum is the 16-bit sum of every byte of the ROM but its own two.
    std::uint32_t sum = 0;
    for ( std::uint8_t const byte : rom.first( rom.size() - 2 ) )
    {
      sum += byte;
    }
    auto const stated = static_cast<std::uint16_t>( header[CHECKSUM] | ( header[CHECKSUM + 1] << 8U ) );
    info["game_version"] = header[GAME_VERSION];
    info["checksum"] = stated;
    info["checksum_ok"] = stated == static_cast<std::uint16_t>( sum );
    info["header_hex"] = hexOf( header );
  }
  info["save_ram_size"] = machine.memory( machine::Memory::SAVE_RAM ).size();
  info["cart_eeprom_size"] = machine.memory( machine::Memory::CART_EEPROM ).size();
  info["internal_eeprom_size"] = machine.memory( machine::Memory::INTERNAL_EEPROM ).size();
  info["rtc"] = machine.hasRtc();
  info["boot_rom"] = emulator.bootRomUsed();
  if ( emulator.bootRomUsed() )
  {
    info["boot_rom_crc32"] = crcText( machine.bootRomCrc() );
  }
  return info;
}

} // namespace

void addCartMethods( Dispatcher& dispatcher, Emulator& emulator )
{
  dispatcher.add(
      "cart.load",
      info( "Puts a cartridge in and powers the machine up with it, running the boot ROM when the BIOS directory "
            "has the model's. The saves of the cartridge that ran are written back first, and the new one's read "
            "from the save directory. Without model, a cartridge whose header supports colour, or a .wsc file, "
            "runs on a WonderSwan Color, any other on a WonderSwan. On failure what ran runs on. Answers as "
            "cart.info.",
            { { .name = "path", .type = "string", .description = "The ROM file, .ws or .wsc." },
              { .name = "model",
                .type = "string",
                .description = "ws (WonderSwan), wsc (WonderSwan Color) or sc (SwanCrystal); chosen by the "
                               "cartridge when left out.",
                .required = false } } ),
      [&emulator]( Json const& params ) -> Outcome
      {
        auto const path = requireString( params, "path" );
        if ( !path )
        {
          return std::unexpected( path.error() );
        }
        std::optional<machine::Model> model;
        if ( params.contains( "model" ) )
        {
          auto const name = requireString( params, "model" );
          model = name ? modelFromName( *name ) : std::nullopt;
          if ( !model )
          {
            return std::unexpected( badRequest( "model is ws, wsc or sc" ) );
          }
        }
        std::error_code error;
        if ( !std::filesystem::is_regular_file( *path, error ) )
        {
          return std::unexpected( Error{ .code = "not_found", .message = fmt::format( "No file {}", *path ) } );
        }
        if ( auto const loaded = emulator.loadCartridge( *path, model ); !loaded )
        {
          return std::unexpected( Error{ .code = "load_failed", .message = loaded.error() } );
        }
        return cartJson( emulator, *emulator.machine() );
      } );

  dispatcher.add(
      "cart.info",
      info( "The cartridge the machine runs: its path, the model running it, what its header says (publisher, game "
            "id and version, colour support, whether it is held vertically, the checksum and whether the ROM sums "
            "to it, the 16 bytes whole), its ROM's size and CRC-32, the sizes of its SRAM, its EEPROM and the "
            "console's EEPROM, whether it has a real-time clock (a Bandai 2003's), and whether the boot ROM ran, "
            "with its CRC-32." ),
      [&emulator]( Json const& /*params*/ ) -> Outcome
      {
        auto const machine = machineOf( emulator );
        if ( !machine )
        {
          return std::unexpected( machine.error() );
        }
        return cartJson( emulator, **machine );
      } );

  dispatcher.add(
      "cart.save",
      info( "Writes the cartridge's SRAM or EEPROM, its real-time clock, and the console's EEPROM, to the "
            "save directory now, as a power-up and the emulator's end do. Nothing is written without a save "
            "directory." ),
      [&emulator]( Json const& /*params*/ ) -> Outcome
      {
        auto const machine = machineOf( emulator );
        if ( !machine )
        {
          return std::unexpected( machine.error() );
        }
        if ( auto const flushed = emulator.flushSaves(); !flushed )
        {
          return std::unexpected( Error{ .code = "save_failed", .message = flushed.error() } );
        }
        std::filesystem::path const& directory = emulator.settings().saveDirectory;
        return Json{ { "written", !directory.empty() }, { "save_directory", directory.string() } };
      } );
}

} // namespace ws::control
