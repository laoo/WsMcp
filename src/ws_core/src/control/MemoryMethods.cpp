#include "Methods.hpp"

#include <spdlog/fmt/fmt.h>

#include <array>
#include <optional>
#include <string_view>

namespace ws::control
{

namespace
{

/// The V30MZ's address space, and the largest read or write.
constexpr std::uint64_t ADDRESS_SPACE = 0x100000;
constexpr std::uint64_t MAX_SIZE = ADDRESS_SPACE;

/// A region memory.read and memory.write address: the CPU's address space,
/// or one of the machine's memories, by offset.
struct Region
{
  std::string_view name;
  /// None for the CPU's address space.
  std::optional<machine::Memory> memory;
  bool writable;
  std::string_view holds;
};

constexpr std::array<Region, 7> REGIONS{ {
    { .name = "cpu",
      .memory = std::nullopt,
      .writable = true,
      .holds = "The V30MZ's 20-bit address space, as it reads it now; the region when none is named. Written as a "
               "debugger writes: only where RAM or SRAM is mapped." },
    { .name = "work_ram",
      .memory = machine::Memory::WORK_RAM,
      .writable = true,
      .holds = "The console's RAM: 16 KB on a WonderSwan, 64 KB on a Color or SwanCrystal." },
    { .name = "rom",
      .memory = machine::Memory::ROM,
      .writable = false,
      .holds = "The cartridge's ROM, whole, its header in its last 16 bytes." },
    { .name = "save_ram",
      .memory = machine::Memory::SAVE_RAM,
      .writable = true,
      .holds = "The cartridge's battery-backed SRAM; empty when it has none." },
    { .name = "boot_rom",
      .memory = machine::Memory::BOOT_ROM,
      .writable = false,
      .holds = "The IPL: 4 KB on a WonderSwan, 8 KB on a Color or SwanCrystal; empty when it is not run." },
    { .name = "internal_eeprom",
      .memory = machine::Memory::INTERNAL_EEPROM,
      .writable = true,
      .holds = "The console's EEPROM: the owner's name and settings." },
    { .name = "cart_eeprom",
      .memory = machine::Memory::CART_EEPROM,
      .writable = true,
      .holds = "The cartridge's EEPROM; empty when it has none." },
} };

/// The region `params` name, `cpu` when they name none.
std::expected<Region, Error> regionOf( Json const& params )
{
  if ( !params.contains( "region" ) )
  {
    return REGIONS.front();
  }
  auto const name = requireString( params, "region" );
  if ( !name )
  {
    return std::unexpected( name.error() );
  }
  for ( Region const& region : REGIONS )
  {
    if ( region.name == *name )
    {
      return region;
    }
  }
  return std::unexpected( Error{ .code = "invalid_region", .message = fmt::format( "No region {}", *name ) } );
}

std::uint64_t sizeOf( machine::Machine const& machine, Region const& region )
{
  return region.memory ? machine.memory( *region.memory ).size() : ADDRESS_SPACE;
}

/// Where the bytes start: in the CPU's space an address, linear or
/// "SSSS:OOOO"; in a memory an offset.
std::expected<std::uint64_t, Error> startOf( Json const& params, Region const& region )
{
  if ( region.memory )
  {
    return requireUnsigned( params, "address" );
  }
  auto const address = requireAddress( params, "address" );
  if ( !address )
  {
    return std::unexpected( address.error() );
  }
  return address->linear;
}

/// The range [address, address + size) checked whole against `region`.
std::expected<void, Error>
checkRange( machine::Machine const& machine, Region const& region, std::uint64_t address, std::uint64_t size )
{
  std::uint64_t const regionSize = sizeOf( machine, region );
  if ( size == 0 || size > MAX_SIZE || address >= regionSize || size > regionSize - address )
  {
    return std::unexpected(
        Error{ .code = "invalid_range",
               .message = fmt::format(
                   "{} bytes from {:#x} are not all in {}, {:#x} bytes", size, address, region.name, regionSize ) } );
  }
  return {};
}

} // namespace

void addMemoryMethods( Dispatcher& dispatcher, Emulator& emulator )
{
  dispatcher.add( "memory.list_regions",
                  info( "The memory regions memory.read and memory.write address, what each holds, whether it can "
                        "be written, and its size in the machine running (null when none runs)." ),
                  [&emulator]( Json const& /*params*/ ) -> Outcome
                  {
                    machine::Machine const* const machine = emulator.machine();
                    Json regions = Json::array();
                    for ( Region const& region : REGIONS )
                    {
                      regions.push_back(
                          Json{ { "name", region.name },
                                { "size", machine != nullptr ? Json( sizeOf( *machine, region ) ) : Json( nullptr ) },
                                { "writable", region.writable },
                                { "holds", region.holds } } );
                    }
                    return Json{ { "regions", std::move( regions ) } };
                  } );

  dispatcher.add(
      "memory.read",
      info( "Reads bytes without the side effects a read by the V30MZ has. Without region, from the CPU's 20-bit "
            "address space as it is mapped now (work RAM at $00000, SRAM at $10000, ROM banks at $20000-$FFFFF, the "
            "boot ROM over the top while it is enabled); with region, from one of memory.list_regions' memories, "
            "by offset.",
            { { .name = "address",
                .type = ADDRESS_TYPE,
                .description = "The first byte: without region a linear address, 0 to $FFFFF, or \"SSSS:OOOO\"; "
                               "with region its offset." },
              { .name = "size", .type = "integer", .description = "How many bytes, at most $100000." },
              { .name = "region",
                .type = "string",
                .description = "work_ram, rom, save_ram, boot_rom, internal_eeprom or cart_eeprom; the CPU's "
                               "address space, cpu, when left out.",
                .required = false } } ),
      [&emulator]( Json const& params ) -> Outcome
      {
        auto const machine = machineOf( emulator );
        if ( !machine )
        {
          return std::unexpected( machine.error() );
        }
        auto const region = regionOf( params );
        if ( !region )
        {
          return std::unexpected( region.error() );
        }
        auto const address = startOf( params, *region );
        auto const size = requireUnsigned( params, "size" );
        if ( !address || !size )
        {
          return std::unexpected( !address ? address.error() : size.error() );
        }
        if ( auto const checked = checkRange( **machine, *region, *address, *size ); !checked )
        {
          return std::unexpected( checked.error() );
        }
        std::string hex;
        if ( region->memory )
        {
          std::span<std::uint8_t const> const memory = std::as_const( **machine ).memory( *region->memory );
          hex = hexOf( memory.subspan( static_cast<std::size_t>( *address ), static_cast<std::size_t>( *size ) ) );
        }
        else
        {
          std::vector<std::uint8_t> bytes( static_cast<std::size_t>( *size ) );
          for ( std::size_t i = 0; i < bytes.size(); ++i )
          {
            bytes[i] = ( *machine )->peek( static_cast<std::uint32_t>( *address + i ) );
          }
          hex = hexOf( bytes );
        }
        return Json{ { "region", region->name }, { "address", *address }, { "data_hex", std::move( hex ) } };
      } );

  dispatcher.add(
      "memory.write",
      info( "Writes bytes as a debugger does: no bus cycle is made and no device runs. Without region, into the "
            "CPU's address space, where every byte must fall on RAM or SRAM mapped there; with region, into work_ram, "
            "save_ram, internal_eeprom or cart_eeprom by offset. The whole range is checked first, so it is written "
            "whole or not at all.",
            { { .name = "address",
                .type = ADDRESS_TYPE,
                .description = "The first byte: without region a linear address, 0 to $FFFFF, or \"SSSS:OOOO\"; "
                               "with region its offset." },
              { .name = "data_hex", .type = "string", .description = "The bytes, two hex digits each." },
              { .name = "region",
                .type = "string",
                .description = "work_ram, save_ram, internal_eeprom or cart_eeprom; the CPU's address space when "
                               "left out.",
                .required = false } } ),
      [&emulator]( Json const& params ) -> Outcome
      {
        auto const machine = machineOf( emulator );
        if ( !machine )
        {
          return std::unexpected( machine.error() );
        }
        auto const region = regionOf( params );
        if ( !region )
        {
          return std::unexpected( region.error() );
        }
        if ( !region->writable )
        {
          return std::unexpected(
              Error{ .code = "invalid_region", .message = fmt::format( "{} cannot be written", region->name ) } );
        }
        auto const address = startOf( params, *region );
        auto const text = requireString( params, "data_hex" );
        if ( !address || !text )
        {
          return std::unexpected( !address ? address.error() : text.error() );
        }
        auto const bytes = bytesOfHex( *text );
        if ( !bytes || bytes->empty() )
        {
          return std::unexpected( badRequest( "data_hex is bytes, two hex digits each" ) );
        }
        if ( auto const checked = checkRange( **machine, *region, *address, bytes->size() ); !checked )
        {
          return std::unexpected( checked.error() );
        }
        if ( region->memory )
        {
          std::span<std::uint8_t> const memory = ( *machine )->memory( *region->memory );
          std::ranges::copy( *bytes, memory.begin() + static_cast<std::ptrdiff_t>( *address ) );
        }
        else
        {
          // A byte written back as it reads tells whether RAM or SRAM is there,
          // and changes nothing.
          for ( std::size_t i = 0; i < bytes->size(); ++i )
          {
            auto const at = static_cast<std::uint32_t>( *address + i );
            if ( !( *machine )->poke( at, ( *machine )->peek( at ) ) )
            {
              return std::unexpected( Error{
                  .code = "invalid_range",
                  .message =
                      fmt::format( "Nothing writable is mapped at {:#07x}: only RAM and SRAM can be written", at ) } );
            }
          }
          for ( std::size_t i = 0; i < bytes->size(); ++i )
          {
            ( *machine )->poke( static_cast<std::uint32_t>( *address + i ), ( *bytes )[i] );
          }
        }
        return Json{ { "region", region->name }, { "address", *address }, { "size", bytes->size() } };
      } );
}

} // namespace ws::control
