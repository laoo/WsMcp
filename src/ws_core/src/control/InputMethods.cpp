#include "Methods.hpp"

#include <spdlog/fmt/fmt.h>

#include <array>
#include <optional>
#include <string_view>

namespace ws::control
{

namespace
{

/// Frames a press holds its buttons, and then lets them go: long enough for
/// a game that reads the keypad once a frame, or every other, to see both.
constexpr std::uint64_t HOLD_FRAMES = 6;
constexpr std::uint64_t RELEASE_FRAMES = 6;
constexpr std::uint64_t MAX_FRAMES = 600;

struct ButtonName
{
  std::string_view name;
  machine::Button button;
};

/// The buttons by the names the hardware gives them, first, and the names of
/// the X pad's directions after.
constexpr std::array<ButtonName, 16> BUTTON_NAMES{ {
    { .name = "x1", .button = machine::Button::UP },
    { .name = "x2", .button = machine::Button::RIGHT },
    { .name = "x3", .button = machine::Button::DOWN },
    { .name = "x4", .button = machine::Button::LEFT },
    { .name = "y1", .button = machine::Button::UP2 },
    { .name = "y2", .button = machine::Button::RIGHT2 },
    { .name = "y3", .button = machine::Button::DOWN2 },
    { .name = "y4", .button = machine::Button::LEFT2 },
    { .name = "a", .button = machine::Button::A },
    { .name = "b", .button = machine::Button::B },
    { .name = "start", .button = machine::Button::START },
    { .name = "sound", .button = machine::Button::SOUND },
    { .name = "up", .button = machine::Button::UP },
    { .name = "right", .button = machine::Button::RIGHT },
    { .name = "down", .button = machine::Button::DOWN },
    { .name = "left", .button = machine::Button::LEFT },
} };

/// The hardware's names, one a button.
constexpr std::size_t CANONICAL_NAMES = 12;

std::uint16_t bitOf( machine::Button button )
{
  return static_cast<std::uint16_t>( 1U << static_cast<unsigned>( button ) );
}

Error invalidInput( std::string message )
{
  return Error{ .code = "invalid_input", .message = std::move( message ) };
}

std::expected<machine::Button, Error> buttonNamed( Json const& name )
{
  if ( name.is_string() )
  {
    for ( ButtonName const& entry : BUTTON_NAMES )
    {
      if ( entry.name == name.get_ref<std::string const&>() )
      {
        return entry.button;
      }
    }
  }
  return std::unexpected( invalidInput(
      fmt::format( "No button {}: x1-x4, y1-y4, up, down, left, right, a, b, start or sound", name.dump() ) ) );
}

/// The buttons `params[buttons]` names, a list or one name, as a mask.
std::expected<std::uint16_t, Error> buttonsOf( Json const& params )
{
  auto const field = params.find( "buttons" );
  if ( field == params.end() || !( field->is_array() || field->is_string() ) )
  {
    return std::unexpected( badRequest( "Missing or invalid field: buttons, a list of button names" ) );
  }
  std::uint16_t mask = 0;
  for ( Json const& name : field->is_array() ? *field : Json::array( { *field } ) )
  {
    auto const button = buttonNamed( name );
    if ( !button )
    {
      return std::unexpected( button.error() );
    }
    mask |= bitOf( *button );
  }
  return mask;
}

/// The names of the buttons in `mask`, as the hardware names them.
Json namesOf( std::uint16_t mask )
{
  Json names = Json::array();
  for ( std::size_t i = 0; i < CANONICAL_NAMES; ++i )
  {
    if ( ( mask & bitOf( BUTTON_NAMES.at( i ).button ) ) != 0 )
    {
      names.push_back( BUTTON_NAMES.at( i ).name );
    }
  }
  return names;
}

/// A number of frames from `params[name]`, or `fallback`.
std::expected<std::uint64_t, Error>
framesOf( Json const& params, std::string_view name, std::uint64_t fallback, std::uint64_t least )
{
  if ( !params.contains( name ) )
  {
    return fallback;
  }
  auto const frames = requireUnsigned( params, name );
  if ( !frames || *frames < least || *frames > MAX_FRAMES )
  {
    return std::unexpected( badRequest( fmt::format( "{} is {} to 600 frames", name, least ) ) );
  }
  return frames;
}

/// `total` with `run`'s cycles and frames added, and its stop if it stopped.
void addRun( machine::RunResult& total, machine::RunResult const& run )
{
  std::uint64_t const cycles = total.cycles + run.cycles;
  std::uint64_t const frames = total.frames + run.frames;
  total = run;
  total.cycles = cycles;
  total.frames = frames;
}

} // namespace

void addInputMethods( Dispatcher& dispatcher, Emulator& emulator )
{
  dispatcher.add(
      "input.press",
      info( "Presses buttons together for some frames, lets them go, and runs some frames more, running the machine "
            "as it goes; buttons input.hold holds stay held. Names: x1-x4 (the X pad: x1 up, x2 right, x3 down, x4 "
            "left, also up, right, down, left), y1-y4 (the Y pad, likewise), a, b, start, sound. A vertical game "
            "is held turned and reads the pads turned with it: which button is its up is the game's to say. A "
            "breakpoint or watchpoint that "
            "stops the hold lets the buttons go, and answers so.",
            { { .name = "buttons", .type = "array", .description = R"(The buttons' names: ["a"], ["x1", "b"].)" },
              { .name = "frames",
                .type = "integer",
                .description = "Frames to hold them, 1 to 600; 6 when left out.",
                .required = false },
              { .name = "release_frames",
                .type = "integer",
                .description = "Frames to run after letting them go, 0 to 600; 6 when left out.",
                .required = false } } ),
      [&emulator]( Json const& params ) -> Outcome
      {
        auto const machine = machineOf( emulator );
        if ( !machine )
        {
          return std::unexpected( machine.error() );
        }
        auto const mask = buttonsOf( params );
        if ( !mask )
        {
          return std::unexpected( mask.error() );
        }
        auto const hold = framesOf( params, "frames", HOLD_FRAMES, 1 );
        auto const release = framesOf( params, "release_frames", RELEASE_FRAMES, 0 );
        if ( !hold || !release )
        {
          return std::unexpected( !hold ? hold.error() : release.error() );
        }
        machine::Machine& ws = **machine;
        std::uint16_t const held = ws.buttons();
        machine::RunResult total{ .reason = machine::StopReason::COMPLETED };
        ws.setButtons( static_cast<std::uint16_t>( held | *mask ) );
        addRun( total, ws.runFrames( *hold ) );
        ws.setButtons( held );
        if ( total.reason == machine::StopReason::COMPLETED && *release > 0 )
        {
          addRun( total, ws.runFrames( *release ) );
        }
        return runJson( total );
      } );

  dispatcher.add(
      "input.set",
      info( "Holds a button down, or lets it go, until told otherwise. The machine does not run.",
            { { .name = "button", .type = "string", .description = "The button's name, as input.press takes it." },
              { .name = "down", .type = "boolean", .description = "Down, or up." } } ),
      [&emulator]( Json const& params ) -> Outcome
      {
        auto const machine = machineOf( emulator );
        if ( !machine )
        {
          return std::unexpected( machine.error() );
        }
        if ( !params.contains( "button" ) )
        {
          return std::unexpected( badRequest( "Missing or invalid field: button" ) );
        }
        auto const button = buttonNamed( params.at( "button" ) );
        if ( !button )
        {
          return std::unexpected( button.error() );
        }
        if ( !params.contains( "down" ) || !params.at( "down" ).is_boolean() )
        {
          return std::unexpected( badRequest( "Missing or invalid field: down" ) );
        }
        std::uint16_t const held = ( *machine )->buttons();
        std::uint16_t const bit = bitOf( *button );
        ( *machine )
            ->setButtons( static_cast<std::uint16_t>( params.at( "down" ).get<bool>() ? held | bit : held & ~bit ) );
        return Json{ { "held", namesOf( ( *machine )->buttons() ) } };
      } );

  dispatcher.add( "input.hold",
                  info( "Holds exactly these buttons down, letting go of any other, until told otherwise. The machine "
                        "does not run.",
                        { { .name = "buttons",
                            .type = "array",
                            .description = "The buttons' names, as input.press takes them; [] lets go of all." } } ),
                  [&emulator]( Json const& params ) -> Outcome
                  {
                    auto const machine = machineOf( emulator );
                    if ( !machine )
                    {
                      return std::unexpected( machine.error() );
                    }
                    auto const mask = buttonsOf( params );
                    if ( !mask )
                    {
                      return std::unexpected( mask.error() );
                    }
                    ( *machine )->setButtons( *mask );
                    return Json{ { "held", namesOf( *mask ) } };
                  } );

  dispatcher.add( "input.clear",
                  info( "Lets go of every button the protocol holds." ),
                  [&emulator]( Json const& /*params*/ ) -> Outcome
                  {
                    auto const machine = machineOf( emulator );
                    if ( !machine )
                    {
                      return std::unexpected( machine.error() );
                    }
                    ( *machine )->setButtons( 0 );
                    return Json::object();
                  } );

  dispatcher.add( "input.get",
                  info( "The buttons the protocol holds, by the hardware's names (x1-x4, y1-y4, a, b, start, "
                        "sound), and as a mask. The desktop application's keyboard and pads hold their own beside "
                        "these." ),
                  [&emulator]( Json const& /*params*/ ) -> Outcome
                  {
                    auto const machine = machineOf( emulator );
                    if ( !machine )
                    {
                      return std::unexpected( machine.error() );
                    }
                    std::uint16_t const mask = ( *machine )->buttons();
                    return Json{ { "held", namesOf( mask ) }, { "mask", mask } };
                  } );
}

} // namespace ws::control
