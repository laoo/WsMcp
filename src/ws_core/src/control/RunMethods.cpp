#include "Methods.hpp"

#include <algorithm>
#include <optional>
#include <string>

namespace ws::control
{

namespace
{

/// Twenty seconds of the machine's time.
constexpr std::uint64_t DEFAULT_TIMEOUT = 20 * machine::Machine::CLOCK_RATE;

/// A run's `count`, which must be positive.
std::expected<std::uint64_t, Error> countOf( Json const& params )
{
  auto const count = requireUnsigned( params, "count" );
  if ( count && *count == 0 )
  {
    return std::unexpected( badRequest( "count must be at least 1" ) );
  }
  return count;
}

/// A byte of memory emu.run_until waits for.
struct MemoryEquals
{
  std::uint32_t address{};
  std::uint8_t value{};
};

std::expected<MemoryEquals, Error> memoryEqualsOf( Json const& params )
{
  Json const& field = params.at( "memory_equals" );
  if ( !field.is_object() )
  {
    return std::unexpected( badRequest( R"(memory_equals is {"address", "value"})" ) );
  }
  auto const address = requireAddress( field, "address" );
  if ( !address )
  {
    return std::unexpected( address.error() );
  }
  auto const value = requireUnsigned( field, "value" );
  if ( !value || *value > 0xff )
  {
    return std::unexpected( badRequest( "memory_equals.value is a byte, 0 to $FF" ) );
  }
  return MemoryEquals{ .address = address->linear, .value = static_cast<std::uint8_t>( *value ) };
}

} // namespace

Json stopJson( machine::RunResult const& result )
{
  if ( result.reason == machine::StopReason::BREAKPOINT )
  {
    return Json{ { "breakpoint", { { "address", result.breakpoint } } } };
  }
  if ( result.reason == machine::StopReason::WATCHPOINT )
  {
    machine::WatchHit const& hit = result.watchpoint;
    return Json{ { "watchpoint",
                   { { "address", hit.address },
                     { "port", hit.port },
                     { "access", hit.write ? "write" : "read" },
                     { "word", hit.word },
                     { "value", hit.value },
                     { "cs", hit.cs },
                     { "ip", hit.ip },
                     { "pc", linearOf( hit.cs, hit.ip ) } } } };
  }
  return Json::object();
}

Json runJson( machine::RunResult const& result )
{
  char const* reason = "completed";
  switch ( result.reason )
  {
  case machine::StopReason::COMPLETED:
    break;
  case machine::StopReason::CONDITION:
    reason = "condition_met";
    break;
  case machine::StopReason::TIMEOUT:
    reason = "timeout";
    break;
  case machine::StopReason::BREAKPOINT:
    reason = "breakpoint";
    break;
  case machine::StopReason::WATCHPOINT:
    reason = "watchpoint";
    break;
  }
  Json answer{ { "reason", reason }, { "cycles_executed", result.cycles }, { "frames_executed", result.frames } };
  answer.update( stopJson( result ) );
  return answer;
}

void addRunMethods( Dispatcher& dispatcher, Emulator& emulator, std::shared_ptr<SerialLog> const& serial )
{
  dispatcher.add(
      "emu.run_frames",
      info( "Runs through frame boundaries, where the PPU hands a finished picture over, 75.47 a second, and stops "
            "at the next instruction boundary.",
            { { .name = "count", .type = "integer", .description = "Frame boundaries to run through." } } ),
      [&emulator]( Json const& params ) -> Outcome
      {
        auto const machine = machineOf( emulator );
        if ( !machine )
        {
          return std::unexpected( machine.error() );
        }
        auto const count = countOf( params );
        if ( !count )
        {
          return std::unexpected( count.error() );
        }
        return runJson( ( *machine )->runFrames( *count ) );
      } );

  dispatcher.add( "emu.run_cycles",
                  info( "Runs cycles of the 3.072 MHz clock, and stops at the next instruction boundary.",
                        { { .name = "count", .type = "integer", .description = "Cycles to run at least." } } ),
                  [&emulator]( Json const& params ) -> Outcome
                  {
                    auto const machine = machineOf( emulator );
                    if ( !machine )
                    {
                      return std::unexpected( machine.error() );
                    }
                    auto const count = countOf( params );
                    if ( !count )
                    {
                      return std::unexpected( count.error() );
                    }
                    return runJson( ( *machine )->runCycles( *count ) );
                  } );

  dispatcher.add(
      "emu.run_until",
      info( "Runs until a condition holds, checked at every instruction boundary: the V30MZ about to run the "
            "instruction at pc; a byte of memory holding a value; or the bytes sent to the serial port and not yet "
            "read by serial.read holding a text. Give one or more; any of them stops it, and condition says which.",
            { { .name = "pc",
                .type = ADDRESS_TYPE,
                .description = "Stop before the instruction at this address: linear, 0 to $FFFFF, or \"CS:IP\" "
                               "such as \"F000:FFF0\".",
                .required = false },
              { .name = "memory_equals",
                .type = "object",
                .description = "{\"address\", \"value\"}: stop when the byte at the address, linear or "
                               "\"SSSS:OOOO\", holds the value.",
                .required = false },
              { .name = "serial_contains",
                .type = "string",
                .description = "Stop when the bytes sent to the serial port since the last serial.read hold this "
                               "text.",
                .required = false },
              { .name = "timeout_cycles",
                .type = "integer",
                .description = "Cycles after which to give up; 61,440,000 (20 s) when left out.",
                .required = false } } ),
      [&emulator, serial]( Json const& params ) -> Outcome
      {
        auto const machine = machineOf( emulator );
        if ( !machine )
        {
          return std::unexpected( machine.error() );
        }
        std::optional<std::uint32_t> pc;
        if ( params.contains( "pc" ) )
        {
          auto const address = requireAddress( params, "pc" );
          if ( !address )
          {
            return std::unexpected( address.error() );
          }
          pc = address->linear;
        }
        std::optional<MemoryEquals> memoryEquals;
        if ( params.contains( "memory_equals" ) )
        {
          auto const wanted = memoryEqualsOf( params );
          if ( !wanted )
          {
            return std::unexpected( wanted.error() );
          }
          memoryEquals = *wanted;
        }
        std::optional<std::string> text;
        if ( params.contains( "serial_contains" ) )
        {
          auto const wanted = requireString( params, "serial_contains" );
          if ( !wanted || wanted->empty() )
          {
            return std::unexpected( badRequest( "serial_contains is a text to look for" ) );
          }
          text = *wanted;
        }
        if ( !pc && !memoryEquals && !text )
        {
          return std::unexpected( badRequest( "give pc, memory_equals, serial_contains or more than one" ) );
        }
        std::uint64_t timeout = DEFAULT_TIMEOUT;
        if ( params.contains( "timeout_cycles" ) )
        {
          auto const given = requireUnsigned( params, "timeout_cycles" );
          if ( !given )
          {
            return std::unexpected( given.error() );
          }
          timeout = *given;
        }

        char const* met = nullptr;
        // The serial port's bytes are taken from the machine as they come, into
        // the log serial.read answers from; the log is searched again only when
        // it has grown, and a text in it already stops the run at the first check.
        std::size_t searched = 0;
        auto const serialHolds = [&]
        {
          serial->collect( emulator );
          if ( serial->unread.size() == searched )
          {
            return false;
          }
          searched = serial->unread.size();
          return std::ranges::search(
                     serial->unread, *text, {}, {}, []( char c ) { return static_cast<std::uint8_t>( c ); } )
                     .begin() != serial->unread.end();
        };
        auto const condition = [&]( machine::Machine const& ws )
        {
          if ( pc && ws.pc() == *pc )
          {
            met = "pc";
          }
          else if ( memoryEquals && ws.peek( memoryEquals->address ) == memoryEquals->value )
          {
            met = "memory_equals";
          }
          else if ( text && serialHolds() )
          {
            met = "serial_contains";
          }
          return met != nullptr;
        };
        Json answer = runJson( ( *machine )->runUntil( condition, timeout ) );
        if ( met != nullptr )
        {
          answer["condition"] = met;
        }
        return answer;
      } );
}

} // namespace ws::control
