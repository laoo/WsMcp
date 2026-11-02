#include "Methods.hpp"

#include "cpu/Disassembler.hpp"

#include <spdlog/fmt/fmt.h>

#include <algorithm>
#include <array>

namespace ws::control
{

namespace
{

constexpr std::uint64_t MAX_INSTRUCTIONS = 1000;

/// The most bytes an instruction takes, prefixes included, as the
/// disassembler looks at them.
constexpr std::size_t MAX_INSTRUCTION_SIZE = 8;

/// An instruction as the V30MZ would fetch it at CS:IP, read without side
/// effects, IP wrapping within the segment as it does.
struct Fetched
{
  std::array<std::uint8_t, MAX_INSTRUCTION_SIZE> bytes{};
  machine::Instruction instruction;
};

Fetched fetch( machine::Machine const& machine, std::uint16_t cs, std::uint16_t ip )
{
  Fetched fetched;
  for ( std::size_t i = 0; i < fetched.bytes.size(); ++i )
  {
    fetched.bytes.at( i ) = machine.peek( linearOf( cs, static_cast<std::uint16_t>( ip + i ) ) );
  }
  fetched.instruction = machine::disassemble( fetched.bytes, ip );
  // An instruction is a byte at least, so that a walk through them moves on.
  fetched.instruction.size = std::max<std::uint8_t>( fetched.instruction.size, 1 );
  return fetched;
}

/// The instruction's own bytes.
std::span<std::uint8_t const> bytesOf( Fetched const& fetched )
{
  return std::span<std::uint8_t const>{ fetched.bytes }.first(
      std::min<std::size_t>( fetched.instruction.size, MAX_INSTRUCTION_SIZE ) );
}

Json instructionJson( Fetched const& fetched, std::uint16_t cs, std::uint16_t ip )
{
  return Json{ { "address", linearOf( cs, ip ) },
               { "cs_ip", segmentedText( cs, ip ) },
               { "length", fetched.instruction.size },
               { "bytes_hex", hexOf( bytesOf( fetched ) ) },
               { "text", fetched.instruction.text } };
}

} // namespace

std::string segmentedText( std::uint16_t segment, std::uint16_t offset )
{
  return fmt::format( "{:04X}:{:04X}", segment, offset );
}

std::string flagsText( std::uint16_t flags )
{
  struct Flag
  {
    char name;
    std::uint16_t mask;
  };

  static constexpr std::array<Flag, 9> FLAGS{ { { .name = 'O', .mask = 0x800 },
                                                { .name = 'D', .mask = 0x400 },
                                                { .name = 'I', .mask = 0x200 },
                                                { .name = 'T', .mask = 0x100 },
                                                { .name = 'S', .mask = 0x80 },
                                                { .name = 'Z', .mask = 0x40 },
                                                { .name = 'A', .mask = 0x10 },
                                                { .name = 'P', .mask = 0x04 },
                                                { .name = 'C', .mask = 0x01 } } };
  std::string text;
  for ( Flag const& flag : FLAGS )
  {
    text += ( flags & flag.mask ) != 0 ? flag.name : static_cast<char>( flag.name - 'A' + 'a' );
  }
  return text;
}

void addCpuMethods( Dispatcher& dispatcher, Emulator& emulator )
{
  dispatcher.add(
      "cpu.get_state",
      info( "The V30MZ's registers at the instruction boundary the machine stands at: CS:IP and its linear address "
            "pc, the general registers AX BX CX DX SP BP SI DI, the segments DS ES SS, the flags as a number and as "
            "ODITSZAPC (a capital for a set flag), whether HLT halted it until an interrupt, the instruction at "
            "CS:IP, and the cycle it begins in." ),
      [&emulator]( Json const& /*params*/ ) -> Outcome
      {
        auto const machine = machineOf( emulator );
        if ( !machine )
        {
          return std::unexpected( machine.error() );
        }
        machine::CpuState const cpu = ( *machine )->cpu();
        std::uint16_t const flags = cpu.flags.get();
        Fetched const fetched = fetch( **machine, cpu.cs, cpu.ip );
        return Json{ { "cs", cpu.cs },
                     { "ip", cpu.ip },
                     { "pc", linearOf( cpu.cs, cpu.ip ) },
                     { "cs_ip", segmentedText( cpu.cs, cpu.ip ) },
                     { "ax", cpu.ax },
                     { "bx", cpu.bx },
                     { "cx", cpu.cx },
                     { "dx", cpu.dx },
                     { "sp", cpu.sp },
                     { "bp", cpu.bp },
                     { "si", cpu.si },
                     { "di", cpu.di },
                     { "ds", cpu.ds },
                     { "es", cpu.es },
                     { "ss", cpu.ss },
                     { "flags", flags },
                     { "flags_text", flagsText( flags ) },
                     { "halted", cpu.halted },
                     { "disasm", fetched.instruction.text },
                     { "bytes_hex", hexOf( bytesOf( fetched ) ) },
                     { "cycle", ( *machine )->now() } };
      } );

  dispatcher.add(
      "cpu.disassemble",
      info( "Disassembles instructions as the V30MZ would fetch them, without the side effects of reading, in "
            "Mesen2's syntax (MOV AX, DS:[BX+SI+$04]); a relative jump shows the offset it lands at. Given as "
            "\"SSSS:OOOO\", the walk keeps to that segment, IP wrapping in it; given linear, it is taken in the "
            "current CS when CS reaches it, else as offset (address & $FFFF) of segment (address >> 16) * $1000.",
            { { .name = "address",
                .type = ADDRESS_TYPE,
                .description = R"(Where to start: linear, 0 to $FFFFF, or "CS:IP" such as "F000:FFF0".)" },
              { .name = "count", .type = "integer", .description = "How many instructions, 1 to 1000." } } ),
      [&emulator]( Json const& params ) -> Outcome
      {
        auto const machine = machineOf( emulator );
        if ( !machine )
        {
          return std::unexpected( machine.error() );
        }
        auto const address = requireAddress( params, "address" );
        auto const count = requireUnsigned( params, "count" );
        if ( !address || !count )
        {
          return std::unexpected( !address ? address.error() : count.error() );
        }
        if ( *count == 0 || *count > MAX_INSTRUCTIONS )
        {
          return std::unexpected( badRequest( "count is 1 to 1000" ) );
        }
        std::uint16_t cs = 0;
        std::uint16_t ip = 0;
        if ( address->segment )
        {
          cs = *address->segment;
          ip = address->offset;
        }
        else
        {
          // The current CS when the address is in its 64 KB, so that the
          // offsets read as the code's own.
          std::uint16_t const current = ( *machine )->cpu().cs;
          std::uint32_t const offset = ( address->linear - linearOf( current, 0 ) ) & 0xFFFFFU;
          cs = offset <= 0xFFFF ? current : static_cast<std::uint16_t>( ( address->linear >> 16U ) << 12U );
          ip = offset <= 0xFFFF ? static_cast<std::uint16_t>( offset ) : static_cast<std::uint16_t>( address->linear );
        }
        Json instructions = Json::array();
        for ( std::uint64_t i = 0; i < *count; ++i )
        {
          Fetched const fetched = fetch( **machine, cs, ip );
          instructions.push_back( instructionJson( fetched, cs, ip ) );
          ip = static_cast<std::uint16_t>( ip + fetched.instruction.size );
        }
        return Json{ { "instructions", std::move( instructions ) } };
      } );
}

} // namespace ws::control
