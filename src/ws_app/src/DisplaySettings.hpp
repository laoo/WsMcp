#pragma once

#include <cstdint>
#include <filesystem>
#include <string_view>

namespace ws::app
{

/// How the emulated screen is drawn: through which shader, at what size, and
/// which way up.
struct DisplaySettings
{
  enum class Preset : std::uint8_t
  {
    /// Each dot a block of its colour.
    SHARP,
    /// Each dot a block of its colour, with dark gaps between them.
    LCD
  };

  Preset preset{ Preset::SHARP };
  /// Only whole multiples of the screen's size, or as large as fits.
  bool integerScale{ true };
  /// Whether a cartridge held upright is shown upright: turned a quarter
  /// anticlockwise, as the console is turned to hold it.
  bool turnUpright{ true };
  /// Each 0 to 1: how dark the LCD preset's gaps are, and how much of the
  /// picture before stays in each new one, as the LCD's slow dots keep it.
  float grid{ 0.5F };
  float ghosting{ 0.0F };
};

/// The name of a preset as the interface shows it.
[[nodiscard]] std::string_view nameOf( DisplaySettings::Preset preset );

/// The settings saved at `path`, or the defaults where there are none to read.
[[nodiscard]] DisplaySettings loadDisplaySettings( std::filesystem::path const& path );
void saveDisplaySettings( DisplaySettings const& settings, std::filesystem::path const& path );

} // namespace ws::app
