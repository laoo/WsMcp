#include "Methods.hpp"

#include "cpu/Disassembler.hpp"

#include <spdlog/fmt/fmt.h>

#include <algorithm>
#include <cctype>
#include <optional>
#include <ranges>
#include <string>
#include <vector>

namespace ws::control
{

namespace
{

constexpr std::uint64_t MAX_STEPS = 1'000'000;
constexpr std::uint64_t DEFAULT_TRACE = 32;
constexpr std::uint64_t MAX_TRACE = 4096;
constexpr auto CYCLES_PER_SECOND = static_cast<double>( machine::Machine::CLOCK_RATE );
constexpr double MIN_TRACE_SECONDS = 0.01;
constexpr double MAX_TRACE_SECONDS = 10.0;
constexpr std::uint64_t ADDRESS_SPACE = 0x100000;
constexpr std::uint64_t PORT_SPACE = 0x10000;

/// Whether `params` say the address is a port's: `port`, false when left out.
std::expected<bool, Error> portFlagOf( Json const& params )
{
  if ( !params.contains( "port" ) )
  {
    return false;
  }
  if ( !params.at( "port" ).is_boolean() )
  {
    return std::unexpected( badRequest( "port is true or false" ) );
  }
  return params.at( "port" ).get<bool>();
}

/// A watchpoint's address: a port, 0 to $FFFF, or a memory address, linear
/// or "SSSS:OOOO".
std::expected<std::uint32_t, Error> watchedAddressOf( Json const& params, bool port )
{
  if ( !port )
  {
    auto const address = requireAddress( params, "address" );
    if ( !address )
    {
      return std::unexpected( address.error() );
    }
    return address->linear;
  }
  auto const address = requireUnsigned( params, "address" );
  if ( !address || *address >= PORT_SPACE )
  {
    return std::unexpected( badRequest( "a port's address is 0 to $FFFF" ) );
  }
  return static_cast<std::uint32_t>( *address );
}

std::expected<machine::Access, Error> accessOf( Json const& params )
{
  if ( !params.contains( "access" ) )
  {
    return machine::Access::WRITE;
  }
  auto const name = requireString( params, "access" );
  if ( name && *name == "read" )
  {
    return machine::Access::READ;
  }
  if ( name && *name == "write" )
  {
    return machine::Access::WRITE;
  }
  if ( name && *name == "access" )
  {
    return machine::Access::BOTH;
  }
  return std::unexpected( badRequest( "access is read, write or access" ) );
}

char const* nameOf( machine::Access access )
{
  switch ( access )
  {
  case machine::Access::READ:
    return "read";
  case machine::Access::WRITE:
    return "write";
  case machine::Access::BOTH:
    return "access";
  }
  return "?";
}

/// The instruction `traced` ran, from the bytes noted when it ran.
machine::Instruction disassembled( machine::TracedInstruction const& traced )
{
  machine::Instruction instruction = machine::disassemble( traced.bytes, traced.ip );
  instruction.size = std::clamp<std::uint8_t>( instruction.size, 1, static_cast<std::uint8_t>( traced.bytes.size() ) );
  return instruction;
}

/// An instruction of the history as debug.trace answers it, its bytes and
/// disassembly as they were when it ran.
Json tracedJson( machine::TracedInstruction const& traced,
                 std::uint64_t index,
                 machine::Instruction const& instruction )
{
  return Json{ { "index", index },
               { "cycle", traced.cycle },
               { "pc", linearOf( traced.cs, traced.ip ) },
               { "cs_ip", segmentedText( traced.cs, traced.ip ) },
               { "ax", traced.ax },
               { "bx", traced.bx },
               { "cx", traced.cx },
               { "dx", traced.dx },
               { "sp", traced.sp },
               { "bp", traced.bp },
               { "si", traced.si },
               { "di", traced.di },
               { "ds", traced.ds },
               { "es", traced.es },
               { "ss", traced.ss },
               { "flags", traced.flags },
               { "bytes_hex", hexOf( std::span<std::uint8_t const>{ traced.bytes }.first( instruction.size ) ) },
               { "disasm", instruction.text } };
}

std::string upper( std::string text )
{
  std::ranges::transform(
      text, text.begin(), []( unsigned char c ) { return static_cast<char>( std::toupper( c ) ); } );
  return text;
}

/// A register debug.trace.find may ask for a value of.
struct RegisterFilter
{
  std::string_view name;
  std::uint16_t machine::TracedInstruction::* field;
  std::optional<std::uint16_t> wanted;
};

/// The registers debug.trace.find takes, by name.
std::vector<RegisterFilter> registerFilters()
{
  using T = machine::TracedInstruction;
  return { { .name = "ax", .field = &T::ax, .wanted = std::nullopt },
           { .name = "bx", .field = &T::bx, .wanted = std::nullopt },
           { .name = "cx", .field = &T::cx, .wanted = std::nullopt },
           { .name = "dx", .field = &T::dx, .wanted = std::nullopt },
           { .name = "sp", .field = &T::sp, .wanted = std::nullopt },
           { .name = "bp", .field = &T::bp, .wanted = std::nullopt },
           { .name = "si", .field = &T::si, .wanted = std::nullopt },
           { .name = "di", .field = &T::di, .wanted = std::nullopt },
           { .name = "cs", .field = &T::cs, .wanted = std::nullopt },
           { .name = "ds", .field = &T::ds, .wanted = std::nullopt },
           { .name = "es", .field = &T::es, .wanted = std::nullopt },
           { .name = "ss", .field = &T::ss, .wanted = std::nullopt } };
}

/// An optional index parameter, 0 when left out.
std::expected<std::uint64_t, Error> fromOf( Json const& params )
{
  if ( !params.contains( "from" ) )
  {
    return 0;
  }
  return requireUnsigned( params, "from" );
}

} // namespace

void addDebugMethods( Dispatcher& dispatcher, Emulator& emulator )
{
  dispatcher.add(
      "emu.step",
      info( "Runs V30MZ instructions, one when count is left out, stopping before at a breakpoint or after a "
            "watched access. The cycles a halted CPU waits for an interrupt are not instructions: a step from HLT "
            "runs until the interrupt comes.",
            { { .name = "count",
                .type = "integer",
                .description = "How many instructions, 1 to 1,000,000.",
                .required = false } } ),
      [&emulator]( Json const& params ) -> Outcome
      {
        auto const machine = machineOf( emulator );
        if ( !machine )
        {
          return std::unexpected( machine.error() );
        }
        std::uint64_t count = 1;
        if ( params.contains( "count" ) )
        {
          auto const given = requireUnsigned( params, "count" );
          if ( !given || *given == 0 || *given > MAX_STEPS )
          {
            return std::unexpected( badRequest( "count is 1 to 1,000,000" ) );
          }
          count = *given;
        }
        return runJson( ( *machine )->step( count ) );
      } );

  dispatcher.add(
      "debug.breakpoint.add",
      info( "Stops runs before the instruction at an address. The instruction the machine stands at runs when it "
            "runs on.",
            { { .name = "address",
                .type = ADDRESS_TYPE,
                .description = "The instruction's linear address, 0 to $FFFFF, or its \"CS:IP\" such as "
                               "\"F000:FFF0\"; a breakpoint is by linear address, whatever segment reaches it." } } ),
      [&emulator]( Json const& params ) -> Outcome
      {
        auto const machine = machineOf( emulator );
        if ( !machine )
        {
          return std::unexpected( machine.error() );
        }
        auto const address = requireAddress( params, "address" );
        if ( !address )
        {
          return std::unexpected( address.error() );
        }
        ( *machine )->addBreakpoint( address->linear );
        return Json{ { "address", address->linear } };
      } );

  dispatcher.add( "debug.breakpoint.remove",
                  info( "Removes the breakpoint at an address. Answers whether there was one.",
                        { { .name = "address",
                            .type = ADDRESS_TYPE,
                            .description = "The instruction's linear address, or its \"CS:IP\"." } } ),
                  [&emulator]( Json const& params ) -> Outcome
                  {
                    auto const machine = machineOf( emulator );
                    if ( !machine )
                    {
                      return std::unexpected( machine.error() );
                    }
                    auto const address = requireAddress( params, "address" );
                    if ( !address )
                    {
                      return std::unexpected( address.error() );
                    }
                    return Json{ { "removed", ( *machine )->removeBreakpoint( address->linear ) } };
                  } );

  dispatcher.add( "debug.breakpoint.list",
                  info( "The breakpoints' linear addresses, ascending." ),
                  [&emulator]( Json const& /*params*/ ) -> Outcome
                  {
                    auto const machine = machineOf( emulator );
                    if ( !machine )
                    {
                      return std::unexpected( machine.error() );
                    }
                    return Json{ { "breakpoints", ( *machine )->breakpoints() } };
                  } );

  dispatcher.add(
      "debug.watchpoint.add",
      info( "Stops runs after the instruction that reads or writes data in a range of memory, or of I/O ports with "
            "port: any access of its own beyond fetching its bytes, a DMA's included. One per address and space; a "
            "second replaces the first.",
            { { .name = "address",
                .type = ADDRESS_TYPE,
                .description = "The range's first address: linear, 0 to $FFFFF, or \"SSSS:OOOO\"; with port, a port, "
                               "0 to $FFFF." },
              { .name = "size",
                .type = "integer",
                .description = "Bytes or ports in the range; 1 when left out.",
                .required = false },
              { .name = "access",
                .type = "string",
                .description = "read, write (the default) or access for both.",
                .required = false },
              { .name = "port",
                .type = "boolean",
                .description = "Watch I/O ports rather than memory; false when left out.",
                .required = false } } ),
      [&emulator]( Json const& params ) -> Outcome
      {
        auto const machine = machineOf( emulator );
        if ( !machine )
        {
          return std::unexpected( machine.error() );
        }
        auto const port = portFlagOf( params );
        if ( !port )
        {
          return std::unexpected( port.error() );
        }
        auto const address = watchedAddressOf( params, *port );
        if ( !address )
        {
          return std::unexpected( address.error() );
        }
        auto const access = accessOf( params );
        if ( !access )
        {
          return std::unexpected( access.error() );
        }
        std::uint64_t const space = *port ? PORT_SPACE : ADDRESS_SPACE;
        std::uint64_t size = 1;
        if ( params.contains( "size" ) )
        {
          auto const given = requireUnsigned( params, "size" );
          if ( !given || *given == 0 || *given > space - *address )
          {
            return std::unexpected( badRequest( "size is 1 to the end of the space" ) );
          }
          size = *given;
        }
        ( *machine )
            ->addWatchpoint( machine::Watchpoint{
                .address = *address, .size = static_cast<std::uint32_t>( size ), .access = *access, .port = *port } );
        return Json::object();
      } );

  dispatcher.add( "debug.watchpoint.remove",
                  info( "Removes the watchpoint at an address. Answers whether there was one.",
                        { { .name = "address",
                            .type = ADDRESS_TYPE,
                            .description = "The range's first address, or its first port with port." },
                          { .name = "port",
                            .type = "boolean",
                            .description = "The watchpoint is on I/O ports; false when left out.",
                            .required = false } } ),
                  [&emulator]( Json const& params ) -> Outcome
                  {
                    auto const machine = machineOf( emulator );
                    if ( !machine )
                    {
                      return std::unexpected( machine.error() );
                    }
                    auto const port = portFlagOf( params );
                    if ( !port )
                    {
                      return std::unexpected( port.error() );
                    }
                    auto const address = watchedAddressOf( params, *port );
                    if ( !address )
                    {
                      return std::unexpected( address.error() );
                    }
                    return Json{ { "removed", ( *machine )->removeWatchpoint( *address, *port ) } };
                  } );

  dispatcher.add( "debug.watchpoint.list",
                  info( "The watchpoints: address, size, access, and whether they watch ports." ),
                  [&emulator]( Json const& /*params*/ ) -> Outcome
                  {
                    auto const machine = machineOf( emulator );
                    if ( !machine )
                    {
                      return std::unexpected( machine.error() );
                    }
                    Json watchpoints = Json::array();
                    for ( machine::Watchpoint const& watchpoint : ( *machine )->watchpoints() )
                    {
                      watchpoints.push_back( Json{ { "address", watchpoint.address },
                                                   { "size", watchpoint.size },
                                                   { "access", nameOf( watchpoint.access ) },
                                                   { "port", watchpoint.port } } );
                    }
                    return Json{ { "watchpoints", std::move( watchpoints ) } };
                  } );

  dispatcher.add(
      "debug.trace",
      info( "The history of execution, counted back from now: index 0 is the newest instruction, the one the "
            "machine stands at, 1 the one that ran before it, and so on, over the last seconds "
            "debug.trace.configure sets (0.5 by default). Answers count of them from index from, newest first, each "
            "with the cycle it began in, CS:IP and its linear pc, the registers before it, and its bytes and "
            "disassembly as they were when it ran.",
            { { .name = "count",
                .type = "integer",
                .description = "How many, 0 to 4096; 32 when left out.",
                .required = false },
              { .name = "from",
                .type = "integer",
                .description = "The newest one's index, 0 being the newest kept; 0 when left out.",
                .required = false } } ),
      [&emulator]( Json const& params ) -> Outcome
      {
        auto const machine = machineOf( emulator );
        if ( !machine )
        {
          return std::unexpected( machine.error() );
        }
        std::uint64_t count = DEFAULT_TRACE;
        if ( params.contains( "count" ) )
        {
          auto const given = requireUnsigned( params, "count" );
          if ( !given || *given > MAX_TRACE )
          {
            return std::unexpected( badRequest( "count is 0 to 4096" ) );
          }
          count = *given;
        }
        auto const given = fromOf( params );
        if ( !given )
        {
          return std::unexpected( given.error() );
        }
        std::uint64_t const length = ( *machine )->traceLength();
        std::uint64_t const from = std::min( *given, length );
        count = std::min( count, length - from );
        // Index i is the (length - 1 - i)th from the oldest.
        auto const kept =
            ( *machine )->trace( static_cast<std::size_t>( length - from - count ), static_cast<std::size_t>( count ) );
        Json instructions = Json::array();
        std::uint64_t index = from;
        for ( machine::TracedInstruction const& traced : std::views::reverse( kept ) )
        {
          instructions.push_back( tracedJson( traced, index++, disassembled( traced ) ) );
        }
        return Json{ { "total", length },
                     { "seconds", static_cast<double>( ( *machine )->traceCycles() ) / CYCLES_PER_SECOND },
                     { "instructions", std::move( instructions ) } };
      } );

  dispatcher.add(
      "debug.trace.configure",
      info( "How many seconds of the machine's time the history of execution keeps, 0.01 to 10, from now and after "
            "every power-up. A second is at most 3,072,000 instructions, some 50 bytes each.",
            { { .name = "seconds", .type = "number", .description = "The history's length." } } ),
      [&emulator]( Json const& params ) -> Outcome
      {
        auto const field = params.find( "seconds" );
        if ( field == params.end() || !field->is_number() || field->get<double>() < MIN_TRACE_SECONDS ||
             field->get<double>() > MAX_TRACE_SECONDS )
        {
          return std::unexpected( badRequest( "seconds is 0.01 to 10" ) );
        }
        double const seconds = field->get<double>();
        emulator.setTraceCycles( static_cast<std::uint64_t>( seconds * CYCLES_PER_SECOND ) );
        return Json{ { "seconds", seconds } };
      } );

  dispatcher.add(
      "debug.trace.find",
      info( "Searches the history of execution back from index from, 0 being the newest, for instructions that "
            "meet every condition given, and answers up to limit of them, newest first, as debug.trace does. "
            "next_from continues the search where it stopped, or is null when it reached the oldest.",
            { { .name = "pc",
                .type = ADDRESS_TYPE,
                .description = "The instruction's linear address, or its \"CS:IP\".",
                .required = false },
              { .name = "pc_start",
                .type = "integer",
                .description = "With pc_end: the instruction's linear address is at least this.",
                .required = false },
              { .name = "pc_end",
                .type = "integer",
                .description = "With pc_start: the instruction's linear address is below this, up to $100000.",
                .required = false },
              { .name = "disasm_contains",
                .type = "string",
                .description = "Its disassembly, as it ran, holds this text, case aside: OUT, CALL, [$0400].",
                .required = false },
              { .name = "ax", .type = "integer", .description = "AX before it.", .required = false },
              { .name = "bx", .type = "integer", .description = "BX before it.", .required = false },
              { .name = "cx", .type = "integer", .description = "CX before it.", .required = false },
              { .name = "dx", .type = "integer", .description = "DX before it.", .required = false },
              { .name = "sp", .type = "integer", .description = "SP before it.", .required = false },
              { .name = "bp", .type = "integer", .description = "BP before it.", .required = false },
              { .name = "si", .type = "integer", .description = "SI before it.", .required = false },
              { .name = "di", .type = "integer", .description = "DI before it.", .required = false },
              { .name = "cs", .type = "integer", .description = "CS before it.", .required = false },
              { .name = "ds", .type = "integer", .description = "DS before it.", .required = false },
              { .name = "es", .type = "integer", .description = "ES before it.", .required = false },
              { .name = "ss", .type = "integer", .description = "SS before it.", .required = false },
              { .name = "from",
                .type = "integer",
                .description = "The index to search back from; 0, the newest, when left out.",
                .required = false },
              { .name = "limit",
                .type = "integer",
                .description = "The most matches to answer, 1 to 4096; 32 when left out.",
                .required = false } } ),
      [&emulator]( Json const& params ) -> Outcome
      {
        auto const machine = machineOf( emulator );
        if ( !machine )
        {
          return std::unexpected( machine.error() );
        }
        // The address range: pc alone, or pc_start and pc_end.
        std::uint32_t pcStart = 0;
        auto pcEnd = static_cast<std::uint32_t>( ADDRESS_SPACE );
        if ( params.contains( "pc" ) )
        {
          auto const pc = requireAddress( params, "pc" );
          if ( !pc )
          {
            return std::unexpected( pc.error() );
          }
          pcStart = pc->linear;
          pcEnd = pcStart + 1;
        }
        else if ( params.contains( "pc_start" ) || params.contains( "pc_end" ) )
        {
          auto const first = requireUnsigned( params, "pc_start" );
          auto const end = requireUnsigned( params, "pc_end" );
          if ( !first || !end || *first >= *end || *end > ADDRESS_SPACE )
          {
            return std::unexpected( badRequest( "give pc_start and pc_end, pc_start below pc_end, up to $100000" ) );
          }
          pcStart = static_cast<std::uint32_t>( *first );
          pcEnd = static_cast<std::uint32_t>( *end );
        }
        std::optional<std::string> text;
        if ( params.contains( "disasm_contains" ) )
        {
          auto const given = requireString( params, "disasm_contains" );
          if ( !given || given->empty() )
          {
            return std::unexpected( badRequest( "disasm_contains is a text to look for" ) );
          }
          text = upper( *given );
        }
        auto filters = registerFilters();
        for ( RegisterFilter& filter : filters )
        {
          if ( params.contains( filter.name ) )
          {
            auto const value = requireUnsigned( params, filter.name );
            if ( !value || *value > 0xffff )
            {
              return std::unexpected( badRequest( fmt::format( "{} is a word, 0 to $FFFF", filter.name ) ) );
            }
            filter.wanted = static_cast<std::uint16_t>( *value );
          }
        }
        std::erase_if( filters, []( RegisterFilter const& filter ) { return !filter.wanted; } );
        auto const from = fromOf( params );
        if ( !from )
        {
          return std::unexpected( from.error() );
        }
        std::uint64_t limit = DEFAULT_TRACE;
        if ( params.contains( "limit" ) )
        {
          auto const given = requireUnsigned( params, "limit" );
          if ( !given || *given == 0 || *given > MAX_TRACE )
          {
            return std::unexpected( badRequest( "limit is 1 to 4096" ) );
          }
          limit = *given;
        }

        machine::Machine const* const ws = *machine;
        std::uint64_t const length = ws->traceLength();
        Json matches = Json::array();
        std::uint64_t index = *from;
        for ( ; index < length && matches.size() < limit; ++index )
        {
          machine::TracedInstruction const traced = ws->tracedAt( static_cast<std::size_t>( length - 1 - index ) );
          std::uint32_t const pc = linearOf( traced.cs, traced.ip );
          bool const registersMatch = std::ranges::all_of(
              filters, [&traced]( RegisterFilter const& filter ) { return traced.*filter.field == *filter.wanted; } );
          if ( pc < pcStart || pc >= pcEnd || !registersMatch )
          {
            continue;
          }
          // The disassembly last: it is what costs.
          machine::Instruction const instruction = disassembled( traced );
          if ( text && !upper( instruction.text ).contains( *text ) )
          {
            continue;
          }
          matches.push_back( tracedJson( traced, index, instruction ) );
        }
        return Json{ { "total", length },
                     { "matches", std::move( matches ) },
                     { "next_from", index < length ? Json( index ) : Json( nullptr ) } };
      } );
}

} // namespace ws::control
