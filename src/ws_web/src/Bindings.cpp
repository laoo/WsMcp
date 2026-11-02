// The core's binding to JavaScript, for a page in a browser
// (docs/decisions/0010-the-core-compiles-to-webassembly.md). It is thin on
// purpose: the page writes files into Emscripten's in-memory file system and
// loads them by path, runs frames, and takes the picture and the sound; every
// other capability is the control protocol, through handle().

#include "ws/Emulator.hpp"
#include "ws/control/Dispatcher.hpp"

#include <emscripten/bind.h>
#include <emscripten/val.h>

#include <string>
#include <utility>
#include <vector>

namespace
{

/// The emulator a page drives. The boot ROMs are looked for in /bios and the
/// saves kept in /saves, which the page may mount on IndexedDB.
class WebEmulator
{
public:
  WebEmulator() : mEmulator{ settings() }, mDispatcher{ mEmulator } {}

  /// Puts the cartridge at `path` in, as the model named ("ws", "wsc", "sc"),
  /// or the one it chooses when empty. Answers the error, or empty.
  std::string load( std::string const& path, std::string const& model )
  {
    auto const loaded = mEmulator.loadCartridge( path, ws::modelFromName( model ) );
    return loaded ? std::string{} : loaded.error();
  }

  /// Runs a frame; answers the frames run, 0 without a machine.
  double runFrame()
  {
    ws::machine::Machine* const machine = mEmulator.machine();
    if ( machine == nullptr )
    {
      return 0;
    }
    return static_cast<double>( machine->runFrames( 1 ).frames );
  }

  void setButtons( unsigned buttons )
  {
    if ( ws::machine::Machine* const machine = mEmulator.machine() )
    {
      machine->setHostButtons( static_cast<std::uint16_t>( buttons ) );
    }
  }

  /// The last picture's RGBA, a view of the module's memory valid until the
  /// next frame.
  [[nodiscard]] emscripten::val picture() const
  {
    ws::machine::Machine const* const machine = mEmulator.machine();
    if ( machine == nullptr )
    {
      return emscripten::val::null();
    }
    auto const pixels = machine->picture();
    return emscripten::val( emscripten::typed_memory_view( pixels.size(), pixels.data() ) );
  }

  [[nodiscard]] unsigned width() const
  {
    ws::machine::Machine const* const machine = mEmulator.machine();
    return machine == nullptr ? ws::machine::SCREEN_WIDTH : machine->pictureWidth();
  }

  [[nodiscard]] unsigned height() const
  {
    ws::machine::Machine const* const machine = mEmulator.machine();
    return machine == nullptr ? ws::machine::SCREEN_HEIGHT : machine->pictureHeight();
  }

  /// The last run's sound: interleaved left and right samples at 24 kHz, as
  /// 16-bit integers, a view valid until the next run.
  [[nodiscard]] emscripten::val audio() const
  {
    ws::machine::Machine const* const machine = mEmulator.machine();
    if ( machine == nullptr )
    {
      return emscripten::val::null();
    }
    auto const frames = machine->audio();
    return emscripten::val(
        emscripten::typed_memory_view( frames.size() * 2, reinterpret_cast<std::int16_t const*>( frames.data() ) ) );
  }

  /// Whether the cartridge asks to be held upright.
  [[nodiscard]] bool vertical() const
  {
    ws::machine::Machine const* const machine = mEmulator.machine();
    return machine != nullptr && machine->vertical();
  }

  /// Writes the saves to /saves.
  std::string flushSaves()
  {
    auto const flushed = mEmulator.flushSaves();
    return flushed ? std::string{} : flushed.error();
  }

  /// A request of the control protocol, as one line of JSON; its answer.
  [[nodiscard]] std::string handle( std::string const& request ) const
  {
    auto const parsed = ws::control::Json::parse( request, nullptr, false );
    return mDispatcher.handle( parsed.is_discarded() ? ws::control::Json{} : parsed ).dump();
  }

private:
  static ws::Settings settings()
  {
    ws::Settings settings;
    settings.biosDirectory = "/bios";
    settings.saveDirectory = "/saves";
    settings.stateDirectory = "/states";
    return settings;
  }

  ws::Emulator mEmulator;
  ws::control::Dispatcher mDispatcher;
};

} // namespace

EMSCRIPTEN_BINDINGS( wsmcp )
{
  emscripten::class_<WebEmulator>( "WsMcp" )
      .constructor<>()
      .function( "load", &WebEmulator::load )
      .function( "runFrame", &WebEmulator::runFrame )
      .function( "setButtons", &WebEmulator::setButtons )
      .function( "picture", &WebEmulator::picture )
      .function( "width", &WebEmulator::width )
      .function( "height", &WebEmulator::height )
      .function( "audio", &WebEmulator::audio )
      .function( "vertical", &WebEmulator::vertical )
      .function( "flushSaves", &WebEmulator::flushSaves )
      .function( "handle", &WebEmulator::handle );
}
