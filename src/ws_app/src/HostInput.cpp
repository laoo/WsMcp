#include "HostInput.hpp"

#include "ws/control/Dispatcher.hpp"

#include <spdlog/spdlog.h>

#include <fstream>
#include <utility>

namespace ws::app
{

namespace
{

using machine::Button;

/// How far a stick is pushed before it counts as a direction: about half way.
constexpr std::int16_t STICK_THRESHOLD = 16'384;

constexpr std::uint16_t bitOf( Button button )
{
  return static_cast<std::uint16_t>( 1U << std::to_underlying( button ) );
}

/// The bindings in the order the Input window lists them, with `keys`.
HostInput::Bindings bindingsOf( std::array<SDL_Keycode, HostInput::BINDINGS> const& keys )
{
  return { { { .name = "X1", .button = Button::UP, .key = keys[0] },
             { .name = "X2", .button = Button::RIGHT, .key = keys[1] },
             { .name = "X3", .button = Button::DOWN, .key = keys[2] },
             { .name = "X4", .button = Button::LEFT, .key = keys[3] },
             { .name = "Y1", .button = Button::UP2, .key = keys[4] },
             { .name = "Y2", .button = Button::RIGHT2, .key = keys[5] },
             { .name = "Y3", .button = Button::DOWN2, .key = keys[6] },
             { .name = "Y4", .button = Button::LEFT2, .key = keys[7] },
             { .name = "A", .button = Button::A, .key = keys[8] },
             { .name = "B", .button = Button::B, .key = keys[9] },
             { .name = "Start", .button = Button::START, .key = keys[10] },
             { .name = "Sound", .button = Button::SOUND, .key = keys[11] } } };
}

char const* nameOf( HostInput::Way way )
{
  return way == HostInput::Way::FLAT ? "flat" : "upright";
}

/// Up, right, down and left, as a gamepad's stick or pad points them.
struct Directions
{
  bool up{};
  bool right{};
  bool down{};
  bool left{};
};

Directions stickOf( SDL_Gamepad* pad, SDL_GamepadAxis xAxis, SDL_GamepadAxis yAxis )
{
  std::int16_t const x = SDL_GetGamepadAxis( pad, xAxis );
  std::int16_t const y = SDL_GetGamepadAxis( pad, yAxis );
  return { .up = y<-STICK_THRESHOLD, .right = x> STICK_THRESHOLD,
           .down = y > STICK_THRESHOLD,
           .left = x < -STICK_THRESHOLD };
}

/// `directions` as the four buttons of a pad, `up` to `left` in turn.
std::uint16_t padOf( Directions directions, std::array<Button, 4> const& pad )
{
  std::uint16_t mask = 0;
  mask |= directions.up ? bitOf( pad[0] ) : 0U;
  mask |= directions.right ? bitOf( pad[1] ) : 0U;
  mask |= directions.down ? bitOf( pad[2] ) : 0U;
  mask |= directions.left ? bitOf( pad[3] ) : 0U;
  return mask;
}

/// What a gamepad holds. Held flat, its pad and left stick are the X pad, its
/// right stick the Y pad, and east and south A and B, where they are on the
/// console. Turned upright, the console's left goes to the bottom: its pad and
/// left stick are the Y pad, its face buttons and right stick the X pad, each
/// turned a quarter, and A and B, now on top, are its shoulders.
std::uint16_t buttonsOf( SDL_Gamepad* pad, HostInput::Way way )
{
  auto const pressed = [pad]( SDL_GamepadButton button ) { return SDL_GetGamepadButton( pad, button ); };
  Directions const left = stickOf( pad, SDL_GAMEPAD_AXIS_LEFTX, SDL_GAMEPAD_AXIS_LEFTY );
  Directions const leftOrPad{ .up = left.up || pressed( SDL_GAMEPAD_BUTTON_DPAD_UP ),
                              .right = left.right || pressed( SDL_GAMEPAD_BUTTON_DPAD_RIGHT ),
                              .down = left.down || pressed( SDL_GAMEPAD_BUTTON_DPAD_DOWN ),
                              .left = left.left || pressed( SDL_GAMEPAD_BUTTON_DPAD_LEFT ) };
  Directions const right = stickOf( pad, SDL_GAMEPAD_AXIS_RIGHTX, SDL_GAMEPAD_AXIS_RIGHTY );

  std::uint16_t mask = 0;
  mask |= pressed( SDL_GAMEPAD_BUTTON_START ) ? bitOf( Button::START ) : 0U;
  mask |= pressed( SDL_GAMEPAD_BUTTON_BACK ) ? bitOf( Button::SOUND ) : 0U;
  if ( way == HostInput::Way::FLAT )
  {
    mask |= padOf( leftOrPad, { Button::UP, Button::RIGHT, Button::DOWN, Button::LEFT } );
    mask |= padOf( right, { Button::UP2, Button::RIGHT2, Button::DOWN2, Button::LEFT2 } );
    mask |= pressed( SDL_GAMEPAD_BUTTON_EAST ) ? bitOf( Button::A ) : 0U;
    mask |= pressed( SDL_GAMEPAD_BUTTON_SOUTH ) ? bitOf( Button::B ) : 0U;
    return mask;
  }
  std::array<Button, 4> const xTurned{ Button::RIGHT, Button::DOWN, Button::LEFT, Button::UP };
  Directions const face{ .up = pressed( SDL_GAMEPAD_BUTTON_NORTH ),
                         .right = pressed( SDL_GAMEPAD_BUTTON_EAST ),
                         .down = pressed( SDL_GAMEPAD_BUTTON_SOUTH ),
                         .left = pressed( SDL_GAMEPAD_BUTTON_WEST ) };
  mask |= padOf( leftOrPad, { Button::RIGHT2, Button::DOWN2, Button::LEFT2, Button::UP2 } );
  mask |= padOf( face, xTurned );
  mask |= padOf( right, xTurned );
  mask |= pressed( SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER ) ? bitOf( Button::A ) : 0U;
  mask |= pressed( SDL_GAMEPAD_BUTTON_LEFT_SHOULDER ) ? bitOf( Button::B ) : 0U;
  return mask;
}

} // namespace

// Mesen2's keys for its WonderSwan controller, the X pad on the arrows and the
// Y pad on WASD, and turned upright the other way about, its pads turned with
// the console; Start on Return, where a player looks for it.
HostInput::HostInput()
    : mBindings{ bindingsOf( { SDLK_UP,
                               SDLK_RIGHT,
                               SDLK_DOWN,
                               SDLK_LEFT,
                               SDLK_W,
                               SDLK_D,
                               SDLK_S,
                               SDLK_A,
                               SDLK_X,
                               SDLK_Z,
                               SDLK_RETURN,
                               SDLK_Q } ),
                 bindingsOf( { SDLK_A,
                               SDLK_W,
                               SDLK_D,
                               SDLK_S,
                               SDLK_LEFT,
                               SDLK_UP,
                               SDLK_RIGHT,
                               SDLK_DOWN,
                               SDLK_Z,
                               SDLK_X,
                               SDLK_RETURN,
                               SDLK_Q } ) }
{
}

HostInput::~HostInput()
{
  for ( auto const& [id, pad] : mGamepads )
  {
    SDL_CloseGamepad( pad );
  }
}

void HostInput::load( std::filesystem::path const& path )
{
  std::ifstream stream{ path };
  if ( !stream )
  {
    return;
  }
  auto const json = control::Json::parse( stream, nullptr, false );
  if ( !json.is_object() )
  {
    spdlog::warn( "{} is not key bindings; the defaults are used", path.string() );
    return;
  }
  for ( Way const way : { Way::FLAT, Way::UPRIGHT } )
  {
    if ( !json.contains( nameOf( way ) ) || !json.at( nameOf( way ) ).is_object() )
    {
      continue;
    }
    auto const& keys = json.at( nameOf( way ) );
    for ( Binding& binding : mBindings.at( std::to_underlying( way ) ) )
    {
      std::string const name{ binding.name };
      if ( keys.contains( name ) && keys.at( name ).is_string() )
      {
        binding.key = SDL_GetKeyFromName( keys.at( name ).get<std::string>().c_str() );
      }
    }
  }
}

void HostInput::save( std::filesystem::path const& path ) const
{
  control::Json json = control::Json::object();
  for ( Way const way : { Way::FLAT, Way::UPRIGHT } )
  {
    control::Json keys = control::Json::object();
    for ( Binding const& binding : mBindings.at( std::to_underlying( way ) ) )
    {
      keys[std::string{ binding.name }] = binding.key == SDLK_UNKNOWN ? "" : SDL_GetKeyName( binding.key );
    }
    json[nameOf( way )] = std::move( keys );
  }
  std::ofstream stream{ path };
  stream << json.dump( 2 ) << '\n';
  if ( !stream )
  {
    spdlog::warn( "cannot write the key bindings to {}", path.string() );
  }
}

void HostInput::handle( SDL_Event const& event, bool toMachine )
{
  if ( event.type == SDL_EVENT_GAMEPAD_ADDED )
  {
    if ( SDL_Gamepad* const pad = SDL_OpenGamepad( event.gdevice.which ); pad != nullptr )
    {
      mGamepads[event.gdevice.which] = pad;
    }
    return;
  }
  if ( event.type == SDL_EVENT_GAMEPAD_REMOVED )
  {
    if ( auto const found = mGamepads.find( event.gdevice.which ); found != mGamepads.end() )
    {
      SDL_CloseGamepad( found->second );
      mGamepads.erase( found );
    }
    return;
  }
  if ( event.type == SDL_EVENT_KEY_DOWN && mCapturing )
  {
    if ( event.key.key != SDLK_ESCAPE )
    {
      mBindings.at( std::to_underlying( mCapturing->way ) ).at( mCapturing->index ).key = event.key.key;
    }
    mCapturing.reset();
    return;
  }
  if ( event.type == SDL_EVENT_KEY_UP )
  {
    mHeld.erase( event.key.scancode );
    return;
  }
  // The application's own shortcuts are not the console's.
  if ( !toMachine || event.type != SDL_EVENT_KEY_DOWN || event.key.repeat ||
       ( event.key.mod & ( SDL_KMOD_GUI | SDL_KMOD_CTRL ) ) != 0 )
  {
    return;
  }
  SDL_Keycode const key = event.key.key == SDLK_KP_ENTER ? SDLK_RETURN : event.key.key;
  for ( Binding const& binding : mBindings.at( std::to_underlying( mWay ) ) )
  {
    if ( binding.key == key && key != SDLK_UNKNOWN )
    {
      mHeld[event.key.scancode] = binding.button;
      return;
    }
  }
}

void HostInput::releaseAll()
{
  mHeld.clear();
}

void HostInput::setWay( Way way )
{
  mWay = way;
}

HostInput::Way HostInput::way() const
{
  return mWay;
}

std::uint16_t HostInput::buttons() const
{
  std::uint16_t mask = 0;
  for ( auto const& [scancode, button] : mHeld )
  {
    mask |= bitOf( button );
  }
  for ( auto const& [id, pad] : mGamepads )
  {
    mask |= buttonsOf( pad, mWay );
  }
  return mask;
}

HostInput::Bindings const& HostInput::bindings( Way way ) const
{
  return mBindings.at( std::to_underlying( way ) );
}

void HostInput::capture( Way way, std::size_t index )
{
  mCapturing = Capture{ .way = way, .index = index };
}

bool HostInput::capturing( Way way, std::size_t index ) const
{
  return mCapturing && mCapturing->way == way && mCapturing->index == index;
}

bool HostInput::capturing() const
{
  return mCapturing.has_value();
}

std::vector<std::string> HostInput::gamepads() const
{
  std::vector<std::string> names;
  for ( auto const& [id, pad] : mGamepads )
  {
    char const* const name = SDL_GetGamepadName( pad );
    names.emplace_back( name != nullptr ? name : "Gamepad" );
  }
  return names;
}

} // namespace ws::app
