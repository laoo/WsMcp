#include "DisplaySettings.hpp"

#include "ws/control/Dispatcher.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <fstream>

namespace ws::app
{

namespace
{

constexpr std::array<DisplaySettings::Preset, 2> PRESETS{ DisplaySettings::Preset::SHARP,
                                                          DisplaySettings::Preset::LCD };

/// The setting `json` names under `name`, or `fallback`.
template <typename Setting, std::size_t N>
Setting named( control::Json const& json, char const* name, std::array<Setting, N> const& all, Setting fallback )
{
  if ( !json.contains( name ) || !json.at( name ).is_string() )
  {
    return fallback;
  }
  auto const text = json.at( name ).get<std::string>();
  auto const found = std::ranges::find_if( all, [&text]( Setting setting ) { return nameOf( setting ) == text; } );
  return found == all.end() ? fallback : *found;
}

/// The value `json` holds under `name`, taken to 0 to 1, or `fallback`.
float fraction( control::Json const& json, char const* name, float fallback )
{
  if ( !json.contains( name ) || !json.at( name ).is_number() )
  {
    return fallback;
  }
  return std::clamp( json.at( name ).get<float>(), 0.0F, 1.0F );
}

} // namespace

std::string_view nameOf( DisplaySettings::Preset preset )
{
  switch ( preset )
  {
  case DisplaySettings::Preset::SHARP:
    return "Sharp";
  case DisplaySettings::Preset::LCD:
    return "LCD";
  }
  return "?";
}

DisplaySettings loadDisplaySettings( std::filesystem::path const& path )
{
  DisplaySettings settings;
  std::ifstream stream{ path };
  if ( !stream )
  {
    return settings;
  }
  auto const json = control::Json::parse( stream, nullptr, false );
  if ( !json.is_object() )
  {
    spdlog::warn( "{} is not display settings; the defaults are used", path.string() );
    return settings;
  }
  settings.preset = named( json, "preset", PRESETS, settings.preset );
  settings.integerScale = json.value( "integer_scale", settings.integerScale );
  settings.turnUpright = json.value( "turn_upright", settings.turnUpright );
  settings.grid = fraction( json, "grid", settings.grid );
  settings.ghosting = std::min( fraction( json, "ghosting", settings.ghosting ), MAX_GHOSTING );
  return settings;
}

void saveDisplaySettings( DisplaySettings const& settings, std::filesystem::path const& path )
{
  control::Json const json{ { "preset", nameOf( settings.preset ) },
                            { "integer_scale", settings.integerScale },
                            { "turn_upright", settings.turnUpright },
                            { "grid", settings.grid },
                            { "ghosting", settings.ghosting } };
  std::ofstream stream{ path };
  stream << json.dump( 2 ) << '\n';
  if ( !stream )
  {
    spdlog::warn( "cannot write the display settings to {}", path.string() );
  }
}

} // namespace ws::app
