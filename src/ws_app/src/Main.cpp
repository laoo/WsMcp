// The desktop entry point: everything it does is Application's.

#include "Application.hpp"

#include "ws/Version.hpp"

#include <CLI/CLI.hpp>
#include <SDL3/SDL_main.h>
#include <spdlog/spdlog.h>

#include <cstdint>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <string>
#include <utility>

namespace
{

int run( int argc, char** argv )
{
  CLI::App app{ "WonderSwan and WonderSwan Color emulator", "wsmcp" };
  app.set_version_flag( "--version", std::string{ ws::versionString() } );
  ws::Settings settings;
  app.add_option( "cartridge", settings.cartridge, "The cartridge's ROM, a .ws or .wsc file" )
      ->check( CLI::ExistingFile );
  app.add_option( "--bios-dir",
                  settings.biosDirectory,
                  "Where the boot ROMs are, as bootrom.ws, bootrom.wsc and bootrom_sc.wsc or under No-Intro's names; "
                  "the one chosen last if not given" )
      ->check( CLI::ExistingDirectory );
  bool noBootRom = false;
  app.add_flag( "--no-boot-rom", noBootRom, "Start the cartridge as the boot ROM would leave it, without running it" );
  std::string model;
  app.add_option( "--model", model, "ws, wsc or sc; chosen by the cartridge if not given" )
      ->check( CLI::IsMember( { "ws", "wsc", "sc" } ) );
  app.add_option( "--save-dir",
                  settings.saveDirectory,
                  "Where the cartridge's SRAM or EEPROM and the console's EEPROM are kept between runs; "
                  "beside the application's settings if not given" );
  app.add_option( "--state-dir",
                  settings.stateDirectory,
                  "Where save states are kept; beside the application's settings if not given" )
      ->check( CLI::ExistingDirectory );
  std::string server;
  app.add_option( "--server", server, "Serve JSON-lines to other programs: tcp:PORT, on 127.0.0.1" )
      ->check( []( std::string const& value )
               { return value.starts_with( "tcp:" ) ? std::string{} : std::string{ "expected tcp:PORT" }; } );
  std::uint16_t mcpPort = 0;
  app.add_option( "--mcp-http", mcpPort, "Serve MCP over HTTP on 127.0.0.1:PORT, at /mcp" );
  CLI11_PARSE( app, argc, argv );
  settings.useBootRom = !noBootRom;
  if ( !model.empty() )
  {
    settings.model = ws::modelFromName( model );
  }
  std::uint16_t const linePort =
      server.empty() ? std::uint16_t{ 0 } : static_cast<std::uint16_t>( std::stoul( server.substr( 4 ) ) );

  auto application = ws::app::Application::create( std::move( settings ) );
  if ( !application )
  {
    spdlog::critical( "cannot start: {}", application.error() );
    return 1;
  }
  ( *application )->serve( linePort, mcpPort );
  ( *application )->run();
  return 0;
}

} // namespace

// Nothing may escape main, for the reasons wsmcp-cli's main gives.
int main( int argc, char** argv )
{
  try
  {
    return run( argc, argv );
  }
  catch ( std::exception const& error )
  {
    std::fprintf( stderr, "wsmcp: %s\n", error.what() );
    return 2;
  }
  catch ( ... )
  {
    std::fputs( "wsmcp: internal error: unknown exception\n", stderr );
    return 2;
  }
}
