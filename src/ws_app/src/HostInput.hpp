#pragma once

#include "ws/machine/Types.hpp"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_keycode.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ws::app
{

/// The host's keyboard and gamepads as the WonderSwan's buttons
/// (docs/decisions/0013-the-desktop-application.md). The console is
/// held flat or, for a cartridge that asks for it, turned upright, and its
/// pads turn with it: each way has its own keys, as Mesen2's has, and a
/// gamepad's directions are turned to match.
class HostInput
{
public:
  enum class Way : std::uint8_t
  {
    FLAT,
    UPRIGHT
  };

  /// A button a host key is bound to.
  struct Binding
  {
    /// What it is called in the Input window.
    std::string_view name;
    machine::Button button;
    SDL_Keycode key{ SDLK_UNKNOWN };
  };

  static constexpr std::size_t BINDINGS = machine::BUTTON_COUNT;
  using Bindings = std::array<Binding, BINDINGS>;

  /// The default bindings.
  HostInput();
  /// Closes the gamepads.
  ~HostInput();

  HostInput( HostInput const& ) = delete;
  HostInput& operator=( HostInput const& ) = delete;
  HostInput( HostInput&& ) = delete;
  HostInput& operator=( HostInput&& ) = delete;

  /// The bindings saved at `path`, over the defaults.
  void load( std::filesystem::path const& path );
  void save( std::filesystem::path const& path ) const;

  /// Takes a keyboard or gamepad event. Keys go to the machine only while
  /// `toMachine`; gamepads always do.
  void handle( SDL_Event const& event, bool toMachine );
  /// Lets go of every key: when the screen loses the keyboard.
  void releaseAll();

  /// Which way the console is held: which keys press what, and which way a
  /// gamepad's directions point.
  void setWay( Way way );
  [[nodiscard]] Way way() const;

  /// The buttons held, Button bits.
  [[nodiscard]] std::uint16_t buttons() const;

  [[nodiscard]] Bindings const& bindings( Way way ) const;
  /// The next key pressed becomes binding `index` of `way`; Escape keeps it
  /// as it is.
  void capture( Way way, std::size_t index );
  [[nodiscard]] bool capturing( Way way, std::size_t index ) const;
  [[nodiscard]] bool capturing() const;

  /// The names of the gamepads open.
  [[nodiscard]] std::vector<std::string> gamepads() const;

private:
  std::array<Bindings, 2> mBindings;
  Way mWay{ Way::FLAT };
  /// What each host key down holds, by scancode.
  std::map<SDL_Scancode, machine::Button> mHeld;

  struct Capture
  {
    Way way;
    std::size_t index;
  };

  std::optional<Capture> mCapturing;
  std::map<SDL_JoystickID, SDL_Gamepad*> mGamepads;
};

} // namespace ws::app
