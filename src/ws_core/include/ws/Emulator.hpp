#pragma once

#include "ws/machine/Machine.hpp"

#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace ws
{

/// Where the emulator finds what it is asked to load, and what it builds.
/// Supplied by whoever starts it, so that the core never decides where to
/// look on its own.
struct Settings
{
  /// The directory the boot ROMs are read from, under Mesen2's names
  /// (bootrom.ws, bootrom.wsc, bootrom_sc.wsc) or No-Intro's.
  std::filesystem::path biosDirectory;
  /// Whether to run the boot ROM when there is one; without, the cartridge
  /// starts as the boot ROM would leave it.
  bool useBootRom{ true };
  /// The machine to build; none to choose by the cartridge: a Color for one
  /// that supports colour or is named .wsc, a WonderSwan otherwise.
  std::optional<machine::Model> model;
  /// The cartridge's ROM file.
  std::filesystem::path cartridge;
  /// Where the cartridge's SRAM or EEPROM and the console's EEPROM are kept
  /// between runs; nothing is kept when empty.
  std::filesystem::path saveDirectory;
  /// Where `state.save` and `state.load` keep save states named without a
  /// directory, and `state.list` looks; the working directory when empty.
  std::filesystem::path stateDirectory;
  machine::AudioMode audioMode{ machine::AudioMode::HEADPHONES };
  bool lcdShowIcons{};
};

/// The emulated WonderSwan as a whole: where its boot ROMs, its cartridge and
/// its saves are, and the machine that runs.
class Emulator
{
public:
  /// Powers the machine up when `settings` name a cartridge that can be
  /// read; otherwise there is no machine, and powerUpError() says why.
  explicit Emulator( Settings settings );
  /// Writes the battery-backed memories back.
  ~Emulator();

  Emulator( Emulator const& ) = delete;
  Emulator& operator=( Emulator const& ) = delete;
  Emulator( Emulator&& ) = delete;
  Emulator& operator=( Emulator&& ) = delete;

  [[nodiscard]] Settings const& settings() const;

  /// Powers a new machine up with the files read again: off and on, as the
  /// WonderSwan has no reset. The saves are written back first. On failure
  /// the machine that ran runs on, and the reason is answered.
  std::expected<void, std::string> powerUp();

  /// Puts the cartridge in `path` in, with `model` or the one it chooses,
  /// and powers up. On failure what ran runs on.
  std::expected<void, std::string> loadCartridge( std::filesystem::path const& path,
                                                  std::optional<machine::Model> model );

  /// Reads the boot ROMs from `directory` from now on, and powers up when a
  /// cartridge is in.
  std::expected<void, std::string> setBiosDirectory( std::filesystem::path const& directory );

  /// Writes the cartridge's SRAM or EEPROM and the console's EEPROM to the
  /// save directory, if there is one.
  std::expected<void, std::string> flushSaves();

  /// The boot ROM file for `model` in the BIOS directory, if there is one.
  [[nodiscard]] std::optional<std::filesystem::path> bootRomPath( machine::Model model ) const;

  /// The machine, or null when there is none.
  [[nodiscard]] machine::Machine* machine();
  [[nodiscard]] machine::Machine const* machine() const;
  /// How many times a machine has been powered up: each is a new machine,
  /// whose counts begin afresh.
  [[nodiscard]] std::uint64_t powerUps() const;
  /// Why there is no machine; empty when there is one.
  [[nodiscard]] std::string const& powerUpError() const;
  /// Whether the machine runs the boot ROM, rather than starting where it
  /// would leave the cartridge.
  [[nodiscard]] bool bootRomUsed() const;

  /// How many cycles the history of execution keeps, this machine's and every
  /// one powered up after it.
  void setTraceCycles( std::uint64_t cycles );
  [[nodiscard]] std::uint64_t traceCycles() const;

  /// Whether the frontend's own run is paused. The emulator runs only when it
  /// is asked to; the desktop application runs it frame after frame, unless
  /// this is set.
  [[nodiscard]] bool paused() const;
  void setPaused( bool paused );

private:
  [[nodiscard]] std::filesystem::path savePath() const;
  [[nodiscard]] std::filesystem::path internalEepromPath( machine::Model model ) const;

  Settings mSettings;
  bool mPaused{};
  std::unique_ptr<machine::Machine> mMachine;
  /// The cartridge the machine runs, which settings may have moved past.
  std::filesystem::path mRunningCartridge;
  bool mBootRomUsed{};
  std::uint64_t mPowerUps{};
  std::uint64_t mTraceCycles{ machine::Machine::DEFAULT_TRACE_CYCLES };
  std::string mPowerUpError;
};

/// "WonderSwan", "WonderSwan Color", "SwanCrystal", as the protocol names
/// them: "ws", "wsc", "sc".
[[nodiscard]] char const* modelName( machine::Model model );
[[nodiscard]] std::optional<machine::Model> modelFromName( std::string_view name );

} // namespace ws
