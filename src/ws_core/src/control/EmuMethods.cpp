#include "Methods.hpp"

#include "ws/Version.hpp"

namespace ws::control
{

void addEmuMethods( Dispatcher& dispatcher, Emulator& emulator )
{
  dispatcher.add(
      "emu.status",
      info( "The emulator's version, whether the window's own run is paused, and, when a machine runs, which one "
            "(ws: WonderSwan, wsc: WonderSwan Color, sc: SwanCrystal), the cartridge it runs, whether it ran the "
            "boot ROM, and its time: cycles of its 3.072 MHz clock and frames (75.47 a second) since power-up, and "
            "where the last run stopped at a breakpoint or a watchpoint. When none runs, why." ),
      [&emulator]( Json const& /*params*/ ) -> Outcome
      {
        Json status{ { "version", versionString() }, { "paused", emulator.paused() } };
        if ( machine::Machine const* machine = emulator.machine() )
        {
          status["running"] = true;
          status["model"] = modelName( machine->model() );
          status["cartridge"] = emulator.settings().cartridge.string();
          status["boot_rom"] = emulator.bootRomUsed();
          status["cycles"] = machine->now();
          status["frame"] = machine->frame();
          // A client attaching to a window that stopped by itself sees where.
          status.update( stopJson( machine->lastRun() ) );
        }
        else
        {
          status["running"] = false;
          status["error"] = emulator.powerUpError();
        }
        return status;
      } );

  dispatcher.add( "emu.pause",
                  info( "Pauses the machine where it runs by itself: in the desktop application, which then runs it "
                        "only on request. wsmcp-cli runs only on request anyway." ),
                  [&emulator]( Json const& /*params*/ ) -> Outcome
                  {
                    emulator.setPaused( true );
                    return Json::object();
                  } );

  dispatcher.add( "emu.resume",
                  info( "Lets the machine run by itself again, where it does: in the desktop application." ),
                  [&emulator]( Json const& /*params*/ ) -> Outcome
                  {
                    emulator.setPaused( false );
                    return Json::object();
                  } );

  dispatcher.add(
      "emu.set_bios_directory",
      info( "Reads the boot ROMs from a directory from now on, as bootrom.ws, bootrom.wsc and bootrom_sc.wsc or "
            "under No-Intro's names, and powers the machine up again with the cartridge it runs. On failure the "
            "directory and the machine stay as they were.",
            { { .name = "path", .type = "string", .description = "The directory." } } ),
      [&emulator]( Json const& params ) -> Outcome
      {
        auto const path = requireString( params, "path" );
        if ( !path )
        {
          return std::unexpected( path.error() );
        }
        if ( auto const powered = emulator.setBiosDirectory( *path ); !powered )
        {
          return std::unexpected( Error{ .code = "power_up_failed", .message = powered.error() } );
        }
        return Json{ { "boot_rom", emulator.bootRomUsed() } };
      } );

  dispatcher.add(
      "emu.reset",
      info( "Switches the machine off and on: the WonderSwan has no reset button. The cartridge and the boot ROM are "
            "read again, the saves written back and read again, every part starts as at power-up and time from 0; "
            "breakpoints, watchpoints and the trace start empty. On failure the machine runs on." ),
      [&emulator]( Json const& /*params*/ ) -> Outcome
      {
        if ( auto const powered = emulator.powerUp(); !powered )
        {
          return std::unexpected( Error{ .code = "power_up_failed", .message = powered.error() } );
        }
        return Json{ { "boot_rom", emulator.bootRomUsed() } };
      } );
}

} // namespace ws::control
