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
  /// How dark the LCD preset's gaps are, 0 to 1, and how much of the picture
  /// before stays in each new one, as the LCD's slow dots keep it, 0 to
  /// MAX_GHOSTING.
  float grid{ 0.5F };
  float ghosting{ 0.0F };
};

/// The most ghosting there is: at 1 the picture before would stay whole, and
/// no new one would ever show.
inline constexpr float MAX_GHOSTING = 0.999F;

/// The name of a preset as the interface shows it.
[[nodiscard]] std::string_view nameOf( DisplaySettings::Preset preset );

/// The settings saved at `path`, or the defaults where there are none to read.
[[nodiscard]] DisplaySettings loadDisplaySettings( std::filesystem::path const& path );
void saveDisplaySettings( DisplaySettings const& settings, std::filesystem::path const& path );

} // namespace ws::app
