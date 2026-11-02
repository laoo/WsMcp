#include "Application.hpp"

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlgpu3.h>
#include <spdlog/fmt/fmt.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <system_error>
#include <utility>

namespace ws::app
{

namespace
{

// The window opens with room for the screen several times over, the menu bar
// and a side panel, scaled for the display it opens on.
constexpr int INITIAL_WIDTH = 1280;
constexpr int INITIAL_HEIGHT = 800;

constexpr char const* SCREEN_WINDOW = "Screen";
constexpr char const* STATUS_WINDOW = "Status";
constexpr char const* INPUT_WINDOW = "Input";

/// How many cartridges File > Open recent remembers.
constexpr std::size_t RECENT_CARTRIDGES = 8;

std::string sdlError( std::string const& what )
{
  return what + ": " + SDL_GetError();
}

/// A black screen, until the machine draws one.
std::vector<std::uint8_t> blankScreen()
{
  std::vector<std::uint8_t> rgba( std::size_t{ machine::SCREEN_WIDTH } * machine::SCREEN_HEIGHT * 4, 0 );
  for ( std::size_t alpha = 3; alpha < rgba.size(); alpha += 4 )
  {
    rgba[alpha] = 0xff;
  }
  return rgba;
}

/// Where the application keeps its settings, created if it must be; empty
/// when there is nowhere.
std::filesystem::path preferencesDirectory()
{
  char* const prefPath = SDL_GetPrefPath( "WsMcp", "wsmcp" );
  if ( prefPath == nullptr )
  {
    return {};
  }
  std::filesystem::path directory{ prefPath };
  SDL_free( prefPath );
  return directory;
}

/// The application's settings kept at `path`, or none.
control::Json readSettings( std::filesystem::path const& path )
{
  std::ifstream stream{ path };
  if ( path.empty() || !stream )
  {
    return control::Json::object();
  }
  auto json = control::Json::parse( stream, nullptr, false );
  return json.is_object() ? json : control::Json::object();
}

/// `directory` beside the settings, made if it is not there; empty when it
/// cannot be.
std::filesystem::path besideSettings( std::filesystem::path const& preferences, char const* directory )
{
  if ( preferences.empty() )
  {
    return {};
  }
  std::error_code error;
  std::filesystem::path const path = preferences / directory;
  std::filesystem::create_directories( path, error );
  return error ? std::filesystem::path{} : path;
}

/// What the menus show for a shortcut with `key`.
std::string shortcut( char key )
{
  return fmt::format( "{}+{}", SDL_GetPlatform() == std::string_view{ "macOS" } ? "Cmd" : "Ctrl", key );
}

char const* longModelName( std::string_view model )
{
  if ( model == "ws" )
  {
    return "WonderSwan";
  }
  if ( model == "wsc" )
  {
    return "WonderSwan Color";
  }
  return model == "sc" ? "SwanCrystal" : "?";
}

} // namespace

std::expected<std::unique_ptr<Application>, std::string> Application::create( Settings settings )
{
  if ( !SDL_Init( SDL_INIT_VIDEO | SDL_INIT_GAMEPAD ) )
  {
    return std::unexpected( sdlError( "SDL_Init" ) );
  }

  float const scale = SDL_GetDisplayContentScale( SDL_GetPrimaryDisplay() );
  SDL_Window* const window = SDL_CreateWindow( "WsMcp",
                                               static_cast<int>( INITIAL_WIDTH * scale ),
                                               static_cast<int>( INITIAL_HEIGHT * scale ),
                                               SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY );
  if ( window == nullptr )
  {
    return std::unexpected( sdlError( "SDL_CreateWindow" ) );
  }

  // Every shader format the ImGui backend ships bytecode for, so that SDL may
  // pick the native API: Metal on macOS, Vulkan or D3D12 elsewhere.
  SDL_GPUDevice* const device = SDL_CreateGPUDevice(
      SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_MSL | SDL_GPU_SHADERFORMAT_METALLIB, false, nullptr );
  if ( device == nullptr )
  {
    SDL_DestroyWindow( window );
    return std::unexpected( sdlError( "SDL_CreateGPUDevice" ) );
  }
  if ( !SDL_ClaimWindowForGPUDevice( device, window ) )
  {
    SDL_DestroyGPUDevice( device );
    SDL_DestroyWindow( window );
    return std::unexpected( sdlError( "SDL_ClaimWindowForGPUDevice" ) );
  }
  SDL_SetGPUSwapchainParameters( device, window, SDL_GPU_SWAPCHAINCOMPOSITION_SDR, SDL_GPU_PRESENTMODE_VSYNC );

  // What the command line leaves out is what was chosen last, or kept beside
  // the settings, so that a game's saves outlive the run that made them.
  std::filesystem::path const preferences = preferencesDirectory();
  control::Json kept = readSettings( preferences.empty() ? std::filesystem::path{} : preferences / "settings.json" );
  if ( settings.biosDirectory.empty() && kept.contains( "bios_directory" ) && kept.at( "bios_directory" ).is_string() )
  {
    settings.biosDirectory = kept.at( "bios_directory" ).get<std::string>();
  }
  if ( settings.saveDirectory.empty() )
  {
    settings.saveDirectory = besideSettings( preferences, "saves" );
  }
  if ( settings.stateDirectory.empty() )
  {
    settings.stateDirectory = besideSettings( preferences, "states" );
  }

  // From here on the destructor owns the window and the device.
  std::filesystem::path const cartridge = settings.cartridge;
  std::unique_ptr<Application> application{ new Application{
      window, device, std::move( settings ), preferences, std::move( kept ) } };
  if ( !application->mScreen->valid() )
  {
    return std::unexpected( sdlError( "creating the screen texture" ) );
  }
  if ( !application->mRenderer->valid() )
  {
    return std::unexpected( sdlError( "creating the screen's shaders" ) );
  }
  if ( !cartridge.empty() )
  {
    // Remembered, as one opened in the window is, once it has run.
    auto const status = application->request( "emu.status" );
    if ( status.at( "result" ).value( "running", false ) )
    {
      application->rememberCartridge( cartridge );
    }
    else
    {
      application->mLastError = status.at( "result" ).value( "error", std::string{} );
    }
  }
  return application;
}

Application::Application( SDL_Window* window,
                          SDL_GPUDevice* device,
                          Settings settings,
                          std::filesystem::path const& preferences,
                          control::Json kept )
    : mWindow{ window }, mDevice{ device },
      mScreen{ std::make_unique<GpuTexture>( device, machine::SCREEN_WIDTH, machine::SCREEN_HEIGHT ) },
      mFrame{ blankScreen() }, mModel{ settings.model }, mStateDirectory{ settings.stateDirectory },
      mAudio{ AudioOutput::open() },
      mEmulation{ std::make_unique<EmulationThread>( std::move( settings ), mAudio.get() ) },
      mRenderer{ std::make_unique<ScreenRenderer>( device ) }, mSettings( std::move( kept ) )
{
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_DockingEnable;

  // The layout is kept with the user's preferences rather than in whatever
  // directory the application was started from.
  if ( !preferences.empty() )
  {
    mImguiIniPath = ( preferences / "imgui.ini" ).string();
    mDisplayPath = preferences / "display.json";
    mInputPath = preferences / "input.json";
    mSettingsPath = preferences / "settings.json";
    io.IniFilename = mImguiIniPath.c_str();
    mDisplay = loadDisplaySettings( mDisplayPath );
    mInput.load( mInputPath );
  }
  else
  {
    io.IniFilename = nullptr;
  }

  auto const requester = [this]( std::string const& method, control::Json params )
  { return request( method, std::move( params ) ); };
  mCpu = std::make_unique<CpuWindow>( requester );
  mMemory = std::make_unique<MemoryWindow>( requester );
  mIo = std::make_unique<IoWindow>( requester );
  mHistory = std::make_unique<HistoryWindow>( requester,
                                              [this]( double seconds ) { keepSetting( "history_seconds", seconds ); } );
  if ( mSettings.contains( "history_seconds" ) && mSettings.at( "history_seconds" ).is_number() )
  {
    static_cast<void>( request( "debug.trace.configure", { { "seconds", mSettings.at( "history_seconds" ) } } ) );
  }

  float const scale = SDL_GetDisplayContentScale( SDL_GetPrimaryDisplay() );
  ImGui::StyleColorsDark();
  ImGui::GetStyle().ScaleAllSizes( scale );
  ImGui::GetStyle().FontScaleDpi = scale;

  ImGui_ImplSDL3_InitForSDLGPU( mWindow );
  ImGui_ImplSDLGPU3_InitInfo initInfo{};
  initInfo.Device = mDevice;
  initInfo.ColorTargetFormat = SDL_GetGPUSwapchainTextureFormat( mDevice, mWindow );
  initInfo.MSAASamples = SDL_GPU_SAMPLECOUNT_1;
  ImGui_ImplSDLGPU3_Init( &initInfo );
}

Application::~Application()
{
  SDL_WaitForGPUIdle( mDevice );
  ImGui_ImplSDL3_Shutdown();
  ImGui_ImplSDLGPU3_Shutdown();
  ImGui::DestroyContext();
  mCpu.reset();
  mMemory.reset();
  mIo.reset();
  mHistory.reset();
  mScreen.reset();
  mRenderer.reset();
  // The servers hand requests to the emulation thread: they go first.
  mMcpHttp.reset();
  mMcp.reset();
  mLineServer.reset();
  // The emulation thread feeds the audio stream: it goes first.
  mEmulation.reset();
  mAudio.reset();
  SDL_ReleaseWindowFromGPUDevice( mDevice, mWindow );
  SDL_DestroyGPUDevice( mDevice );
  SDL_DestroyWindow( mWindow );
  SDL_Quit();
}

void Application::run()
{
  while ( !mQuit )
  {
    SDL_Event event;
    while ( SDL_PollEvent( &event ) )
    {
      ImGui_ImplSDL3_ProcessEvent( &event );
      ImGuiIO const& io = ImGui::GetIO();
      mInput.handle( event, mScreenFocused && !io.WantTextInput );
      if ( event.type == SDL_EVENT_DROP_FILE && event.drop.data != nullptr )
      {
        loadCartridge( event.drop.data );
      }
      else if ( event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat &&
                ( event.key.mod & ( SDL_KMOD_GUI | SDL_KMOD_CTRL ) ) != 0 )
      {
        switch ( event.key.key )
        {
        case SDLK_O:
          openCartridge();
          break;
        case SDLK_S:
          saveState();
          break;
        case SDLK_L:
          loadState();
          break;
        case SDLK_R:
          noteError( request( "emu.reset" ) );
          break;
        default:
          break;
        }
      }
      if ( event.type == SDL_EVENT_QUIT ||
           ( event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID == SDL_GetWindowID( mWindow ) ) )
      {
        mQuit = true;
      }
    }

    // A minimised window has no swapchain to present to; waiting here keeps
    // the loop from spinning until it is restored.
    if ( ( SDL_GetWindowFlags( mWindow ) & SDL_WINDOW_MINIMIZED ) != 0 )
    {
      SDL_Delay( 10 );
      continue;
    }

    takeChosen();
    updateEmulation();

    ImGui_ImplSDLGPU3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    drawInterface();
    ImGui::Render();
    renderFrame();
  }
}

control::Json Application::request( std::string const& method, control::Json params )
{
  return mEmulation->handle( control::Json{ { "id", 1 }, { "method", method }, { "params", std::move( params ) } } );
}

bool Application::noteError( control::Json const& response )
{
  bool const ok = response.at( "ok" ) == true;
  mLastError = ok ? std::string{} : response.at( "error" ).at( "message" ).get<std::string>();
  return ok;
}

void Application::serve( std::uint16_t linePort, std::uint16_t mcpPort )
{
  auto const handler = [this]( control::Json const& request ) { return mEmulation->handle( request ); };
  if ( linePort != 0 )
  {
    mLineServer = std::make_unique<server::TcpLineServer>( linePort, handler );
    spdlog::info( "serving JSON-lines on 127.0.0.1:{}", mLineServer->port() );
  }
  if ( mcpPort != 0 )
  {
    mMcp = std::make_unique<server::McpServer>( mEmulation->methods(), handler );
    mMcpHttp = std::make_unique<server::McpHttpServer>( mcpPort, *mMcp );
    spdlog::info( "serving MCP on http://127.0.0.1:{}/mcp", mMcpHttp->port() );
  }
}

void Application::updateEmulation()
{
  mEmulation->setHostButtons( mInput.buttons() );
  if ( mEmulation->takePicture( mPicturesShown, mPicture ) )
  {
    present();
  }
}

void Application::present()
{
  if ( mPicture.rgba.empty() )
  {
    return;
  }
  // Held upright, the console is turned a quarter anticlockwise, as Mesen2
  // turns its picture: the LCD's right edge is the top.
  bool const upright = mPicture.vertical && mDisplay.turnUpright;
  mInput.setWay( upright ? HostInput::Way::UPRIGHT : HostInput::Way::FLAT );
  std::uint32_t const sourceWidth = mPicture.width;
  std::uint32_t const width = upright ? mPicture.height : mPicture.width;
  std::uint32_t const height = upright ? mPicture.width : mPicture.height;
  std::size_t const bytes = std::size_t{ width } * height * 4;
  bool const sameSize = width == mFrameWidth && height == mFrameHeight && mGhost.size() == bytes;
  if ( !sameSize )
  {
    mFrame.assign( bytes, 0 );
    mGhost.assign( bytes, 0.0F );
    mFrameWidth = width;
    mFrameHeight = height;
    mScreen = std::make_unique<GpuTexture>( mDevice, width, height );
  }
  // The LCD's dots are slow to change: some of the picture before shows
  // through the new one.
  float const keep = sameSize ? mDisplay.ghosting : 0.0F;
  for ( std::uint32_t y = 0; y < height; ++y )
  {
    for ( std::uint32_t x = 0; x < width; ++x )
    {
      std::size_t const from = upright ? ( ( std::size_t{ x } * sourceWidth ) + ( sourceWidth - 1 - y ) ) * 4
                                       : ( ( std::size_t{ y } * sourceWidth ) + x ) * 4;
      std::size_t const to = ( ( std::size_t{ y } * width ) + x ) * 4;
      for ( std::size_t channel = 0; channel < 3; ++channel )
      {
        float const now = mPicture.rgba[from + channel];
        float& shown = mGhost[to + channel];
        shown = ( now * ( 1.0F - keep ) ) + ( shown * keep );
        mFrame[to + channel] = static_cast<std::uint8_t>( std::lround( shown ) );
      }
      mFrame[to + 3] = 0xff;
    }
  }
  mFrameChanged = true;
}

void Application::takeChosen()
{
  std::optional<std::filesystem::path> cartridge;
  std::optional<std::filesystem::path> directory;
  {
    std::scoped_lock const lock{ mChosenMutex };
    cartridge.swap( mChosenCartridge );
    directory.swap( mChosenBiosDirectory );
  }
  if ( directory )
  {
    setBiosDirectory( *directory );
  }
  if ( cartridge )
  {
    loadCartridge( *cartridge );
  }
}

void Application::openCartridge()
{
  static constexpr std::array<SDL_DialogFileFilter, 1> FILTERS{ { { .name = "WonderSwan cartridges",
                                                                    .pattern = "ws;wsc" } } };
  SDL_ShowOpenFileDialog(
      []( void* self, char const* const* files, int /*filter*/ )
      {
        // Null on an error, empty when the dialog was cancelled.
        if ( files != nullptr && files[0] != nullptr )
        {
          auto* const application = static_cast<Application*>( self );
          std::scoped_lock const lock{ application->mChosenMutex };
          application->mChosenCartridge = files[0];
        }
      },
      this,
      mWindow,
      FILTERS.data(),
      static_cast<int>( FILTERS.size() ),
      nullptr,
      false );
}

void Application::chooseBiosDirectory()
{
  SDL_ShowOpenFolderDialog(
      []( void* self, char const* const* folders, int /*filter*/ )
      {
        if ( folders != nullptr && folders[0] != nullptr )
        {
          auto* const application = static_cast<Application*>( self );
          std::scoped_lock const lock{ application->mChosenMutex };
          application->mChosenBiosDirectory = folders[0];
        }
      },
      this,
      mWindow,
      nullptr,
      false );
}

void Application::loadCartridge( std::filesystem::path const& path )
{
  control::Json params{ { "path", path.string() } };
  if ( mModel )
  {
    params["model"] = modelName( *mModel );
  }
  if ( noteError( request( "cart.load", std::move( params ) ) ) )
  {
    rememberCartridge( path );
  }
}

void Application::rememberCartridge( std::filesystem::path const& path )
{
  // The newest first, each once.
  control::Json recent = control::Json::array( { path.string() } );
  if ( mSettings.contains( "recent_cartridges" ) && mSettings.at( "recent_cartridges" ).is_array() )
  {
    for ( auto const& kept : mSettings.at( "recent_cartridges" ) )
    {
      if ( kept.is_string() && kept != path.string() && recent.size() < RECENT_CARTRIDGES )
      {
        recent.push_back( kept );
      }
    }
  }
  keepSetting( "recent_cartridges", std::move( recent ) );
}

void Application::setBiosDirectory( std::filesystem::path const& directory )
{
  if ( noteError( request( "emu.set_bios_directory", { { "path", directory.string() } } ) ) )
  {
    keepSetting( "bios_directory", directory.string() );
  }
}

std::optional<std::string> Application::stateFile()
{
  auto const status = request( "emu.status" );
  if ( !status.at( "result" ).value( "running", false ) )
  {
    mLastError = "No cartridge is in";
    return std::nullopt;
  }
  std::filesystem::path const cartridge = status.at( "result" ).at( "cartridge" ).get<std::string>();
  return cartridge.stem().string() + ".wsstate";
}

void Application::saveState()
{
  if ( auto const file = stateFile() )
  {
    noteError( request( "state.save", { { "filename", *file } } ) );
  }
}

void Application::loadState()
{
  if ( auto const file = stateFile() )
  {
    noteError( request( "state.load", { { "filename", *file } } ) );
  }
}

void Application::keepSetting( std::string const& key, control::Json value )
{
  mSettings[key] = std::move( value );
  if ( mSettingsPath.empty() )
  {
    return;
  }
  std::ofstream stream{ mSettingsPath };
  stream << mSettings.dump( 2 ) << '\n';
  if ( !stream )
  {
    spdlog::warn( "cannot write the settings to {}", mSettingsPath.string() );
  }
}

void Application::drawInterface()
{
  drawMenuBar();
  ImGuiID const dockspace = ImGui::DockSpaceOverViewport();
  // On the first run, before imgui.ini has a layout to restore, the screen
  // fills the dockspace and the status panel floats over it.
  ImGui::SetNextWindowDockID( dockspace, ImGuiCond_FirstUseEver );
  drawScreenWindow();
  drawStatusWindow();
  drawInputWindow();
  mCpu->draw( mShowCpu );
  mMemory->draw( mShowMemory );
  mIo->draw( mShowIo );
  mHistory->draw( mShowHistory );
  drawSoundWindow();
}

void Application::drawMenuBar()
{
  if ( !ImGui::BeginMainMenuBar() )
  {
    return;
  }
  if ( ImGui::BeginMenu( "File" ) )
  {
    if ( ImGui::MenuItem( "Open cartridge...", shortcut( 'O' ).c_str() ) )
    {
      openCartridge();
    }
    control::Json const recent = mSettings.value( "recent_cartridges", control::Json::array() );
    if ( ImGui::BeginMenu( "Open recent", !recent.empty() ) )
    {
      for ( auto const& path : recent )
      {
        if ( path.is_string() &&
             ImGui::MenuItem( std::filesystem::path{ path.get<std::string>() }.filename().string().c_str() ) )
        {
          loadCartridge( path.get<std::string>() );
        }
        if ( path.is_string() && ImGui::IsItemHovered() )
        {
          ImGui::SetTooltip( "%s", path.get_ref<std::string const&>().c_str() );
        }
      }
      ImGui::EndMenu();
    }
    if ( ImGui::MenuItem( "Choose boot ROM directory..." ) )
    {
      chooseBiosDirectory();
    }
    ImGui::Separator();
    if ( ImGui::MenuItem( "Quit" ) )
    {
      mQuit = true;
    }
    ImGui::EndMenu();
  }
  if ( ImGui::BeginMenu( "Emulation" ) )
  {
    drawEmulationMenu();
    ImGui::EndMenu();
  }
  if ( ImGui::BeginMenu( "Display" ) )
  {
    drawDisplayMenu();
    ImGui::EndMenu();
  }
  if ( ImGui::BeginMenu( "View" ) )
  {
    ImGui::MenuItem( STATUS_WINDOW, nullptr, &mShowStatus );
    ImGui::MenuItem( INPUT_WINDOW, nullptr, &mShowInput );
    ImGui::MenuItem( "CPU", nullptr, &mShowCpu );
    ImGui::MenuItem( "History", nullptr, &mShowHistory );
    ImGui::MenuItem( "Memory", nullptr, &mShowMemory );
    ImGui::MenuItem( "I/O", nullptr, &mShowIo );
    ImGui::MenuItem( "Sound", nullptr, &mShowSound );
    ImGui::EndMenu();
  }
  ImGui::EndMainMenuBar();
}

void Application::drawEmulationMenu()
{
  // Paused through the protocol, as an agent pauses it, so that the menu
  // shows a pause an agent made too.
  mPaused = mEmulation->paused();
  if ( ImGui::MenuItem( "Pause", nullptr, &mPaused ) )
  {
    static_cast<void>( request( mPaused ? "emu.pause" : "emu.resume" ) );
  }
  if ( ImGui::MenuItem( "Power off and on", shortcut( 'R' ).c_str() ) )
  {
    noteError( request( "emu.reset" ) );
  }
  ImGui::Separator();
  if ( ImGui::MenuItem( "Save state", shortcut( 'S' ).c_str() ) )
  {
    saveState();
  }
  if ( ImGui::MenuItem( "Load state", shortcut( 'L' ).c_str() ) )
  {
    loadState();
  }
  ImGui::Separator();
  // Another model takes the cartridge running out and puts it in again.
  ImGui::TextDisabled( "Model" );
  std::optional<machine::Model> chosen = mModel;
  if ( ImGui::MenuItem( "Chosen by the cartridge", nullptr, !mModel ) )
  {
    chosen.reset();
  }
  for ( auto const model : { machine::Model::MONOCHROME, machine::Model::COLOR, machine::Model::SWAN_CRYSTAL } )
  {
    if ( ImGui::MenuItem( longModelName( modelName( model ) ), nullptr, mModel == model ) )
    {
      chosen = model;
    }
  }
  if ( chosen != mModel )
  {
    mModel = chosen;
    auto const status = request( "emu.status" ).at( "result" );
    if ( status.value( "running", false ) )
    {
      loadCartridge( status.at( "cartridge" ).get<std::string>() );
    }
  }
}

void Application::drawDisplayMenu()
{
  bool changed = false;
  for ( auto const preset : { DisplaySettings::Preset::SHARP, DisplaySettings::Preset::LCD } )
  {
    if ( ImGui::MenuItem( std::string{ nameOf( preset ) }.c_str(), nullptr, mDisplay.preset == preset ) )
    {
      mDisplay.preset = preset;
      changed = true;
    }
  }
  ImGui::Separator();
  if ( ImGui::MenuItem( "Turn upright games upright", nullptr, &mDisplay.turnUpright ) )
  {
    present();
    changed = true;
  }
  changed |= ImGui::MenuItem( "Whole multiples only", nullptr, &mDisplay.integerScale );
  // A slider changes the picture as it is dragged, and is kept when let go.
  auto const slider = [&changed]( char const* label, float& value, float most )
  {
    ImGui::PushID( "setting" );
    ImGui::SliderFloat( label, &value, 0.0F, most, "%.3f", ImGuiSliderFlags_AlwaysClamp );
    changed |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::PopID();
  };
  ImGui::Separator();
  if ( mDisplay.preset == DisplaySettings::Preset::LCD )
  {
    slider( "Grid", mDisplay.grid, 1.0F );
  }
  slider( "Ghosting", mDisplay.ghosting, MAX_GHOSTING );
  if ( changed && !mDisplayPath.empty() )
  {
    saveDisplaySettings( mDisplay, mDisplayPath );
  }
}

void Application::drawScreenWindow()
{
  ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2{ 0.0F, 0.0F } );
  bool const open = ImGui::Begin( SCREEN_WINDOW, nullptr, ImGuiWindowFlags_NoScrollbar );
  ImGui::PopStyleVar();
  bool const focused = ImGui::IsWindowFocused( ImGuiFocusedFlags_RootAndChildWindows );
  if ( mScreenFocused && !focused )
  {
    // Keys held as the screen loses the keyboard would never be let go.
    mInput.releaseAll();
  }
  mScreenFocused = focused;
  mScreenShown = false;
  if ( open )
  {
    // Sized in the display's pixels, which the shader draws, centred; scaled
    // by whole multiples, or as large as fits. The LCD's dots are square.
    float const pixelsPerPoint = ImGui::GetIO().DisplayFramebufferScale.y;
    ImVec2 const available = ImGui::GetContentRegionAvail();
    auto const frameWidth = static_cast<float>( mFrameWidth );
    auto const frameHeight = static_cast<float>( mFrameHeight );
    float scale = std::min( available.x * pixelsPerPoint / frameWidth, available.y * pixelsPerPoint / frameHeight );
    if ( mDisplay.integerScale )
    {
      scale = std::max( 1.0F, std::floor( scale ) );
    }
    auto const width = static_cast<std::uint32_t>( std::lround( frameWidth * scale ) );
    auto const height = static_cast<std::uint32_t>( std::lround( frameHeight * scale ) );
    ImVec2 const size{ static_cast<float>( width ) / pixelsPerPoint, static_cast<float>( height ) / pixelsPerPoint };
    // Centred, on a whole pixel of the display: the shader's output is shown a
    // pixel for a pixel, and half a pixel off would sample some of its rows
    // twice and others not at all.
    ImVec2 const cursor = ImGui::GetCursorScreenPos();
    auto const onPixel = [pixelsPerPoint]( float points )
    { return std::round( points * pixelsPerPoint ) / pixelsPerPoint; };
    ImVec2 const corner{ onPixel( cursor.x + std::max( 0.0F, ( available.x - size.x ) / 2.0F ) ),
                         onPixel( cursor.y + std::max( 0.0F, ( available.y - size.y ) / 2.0F ) ) };
    ImGui::SetCursorScreenPos( corner );

    // The shader's output is shown a pixel for a pixel: sampled nearest, the
    // sampler restored for the rest of the interface.
    if ( SDL_GPUTexture* const target = width > 0 && height > 0 ? mRenderer->target( width, height ) : nullptr )
    {
      ImDrawList* const drawList = ImGui::GetWindowDrawList();
      ImGuiPlatformIO const& platform = ImGui::GetPlatformIO();
      drawList->AddCallback( platform.DrawCallback_SetSamplerNearest, nullptr );
      drawList->AddImage( ImTextureRef{ reinterpret_cast<ImTextureID>( target ) },
                          corner,
                          ImVec2{ corner.x + size.x, corner.y + size.y } );
      ImGui::Dummy( size );
      drawList->AddCallback( platform.DrawCallback_SetSamplerLinear, nullptr );
      mScreenShown = true;
    }
  }
  ImGui::End();
}

void Application::drawStatusWindow()
{
  if ( !mShowStatus )
  {
    return;
  }
  if ( ImGui::Begin( STATUS_WINDOW, &mShowStatus ) )
  {
    // Asked through the dispatcher, as an agent would ask, so that what the
    // window shows is what the protocol answers.
    auto const response = request( "emu.status" );
    if ( response.at( "ok" ) == true )
    {
      auto const& result = response.at( "result" );
      ImGui::Text( "Version: %s", result.at( "version" ).get_ref<std::string const&>().c_str() );
      if ( result.at( "running" ) == true )
      {
        std::filesystem::path const cartridge = result.at( "cartridge" ).get<std::string>();
        ImGui::TextWrapped( "Cartridge: %s", cartridge.filename().string().c_str() );
        ImGui::Text( "%s, %s",
                     longModelName( result.at( "model" ).get<std::string>() ),
                     result.at( "boot_rom" ) == true ? "from its boot ROM" : "without a boot ROM" );
        ImGui::Text( "Frame: %lld", static_cast<long long>( result.at( "frame" ).get<std::int64_t>() ) );
        ImGui::Text( "Cycle: %lld", static_cast<long long>( result.at( "cycles" ).get<std::int64_t>() ) );
        if ( mPicture.vertical )
        {
          ImGui::TextUnformatted( mDisplay.turnUpright ? "Held upright" : "Held upright, shown flat" );
        }
      }
      else
      {
        ImGui::TextWrapped( "Not running: %s", result.at( "error" ).get_ref<std::string const&>().c_str() );
        ImGui::TextWrapped( "Open a cartridge with File > Open cartridge, or drop one on the window." );
      }
      if ( result.at( "paused" ) == true )
      {
        ImGui::TextUnformatted( "Paused" );
      }
      if ( result.contains( "breakpoint" ) )
      {
        ImGui::Text( "Stopped at the breakpoint at $%05X", result.at( "breakpoint" ).at( "address" ).get<unsigned>() );
      }
      if ( result.contains( "watchpoint" ) )
      {
        auto const& watched = result.at( "watchpoint" );
        ImGui::Text( "Stopped after a %s of %s$%05X by %04X:%04X",
                     watched.at( "access" ).get_ref<std::string const&>().c_str(),
                     watched.at( "port" ) == true ? "port " : "",
                     watched.at( "address" ).get<unsigned>(),
                     watched.at( "cs" ).get<unsigned>(),
                     watched.at( "ip" ).get<unsigned>() );
      }
    }
    if ( !mLastError.empty() )
    {
      ImGui::TextWrapped( "%s", mLastError.c_str() );
    }
    ImGui::Text( "Interface: %.1f fps", static_cast<double>( ImGui::GetIO().Framerate ) );
  }
  ImGui::End();
}

void Application::drawInputWindow()
{
  if ( !mShowInput )
  {
    return;
  }
  if ( ImGui::Begin( INPUT_WINDOW, &mShowInput ) )
  {
    ImGui::TextWrapped( "Held %s now. Each way has keys of its own; the screen takes the keys while it has the focus.",
                        mInput.way() == HostInput::Way::FLAT ? "flat" : "upright" );
    if ( ImGui::BeginTable( "bindings", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit ) )
    {
      ImGui::TableSetupColumn( "Button" );
      ImGui::TableSetupColumn( "Flat" );
      ImGui::TableSetupColumn( "##bindflat" );
      ImGui::TableSetupColumn( "Upright" );
      ImGui::TableSetupColumn( "##bindupright" );
      ImGui::TableHeadersRow();
      for ( std::size_t i = 0; i < HostInput::BINDINGS; ++i )
      {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        std::string_view const name = mInput.bindings( HostInput::Way::FLAT ).at( i ).name;
        ImGui::TextUnformatted( name.data(), name.data() + name.size() );
        for ( auto const way : { HostInput::Way::FLAT, HostInput::Way::UPRIGHT } )
        {
          ImGui::TableNextColumn();
          SDL_Keycode const key = mInput.bindings( way ).at( i ).key;
          char const* bound = "-";
          if ( mInput.capturing( way, i ) )
          {
            bound = "press a key...";
          }
          else if ( key != SDLK_UNKNOWN )
          {
            bound = SDL_GetKeyName( key );
          }
          ImGui::TextUnformatted( bound );
          ImGui::TableNextColumn();
          ImGui::PushID( static_cast<int>( ( i * 2 ) + std::to_underlying( way ) ) );
          if ( ImGui::SmallButton( "Bind" ) )
          {
            mInput.capture( way, i );
          }
          ImGui::PopID();
        }
      }
      ImGui::EndTable();
    }
    if ( !mInputPath.empty() && !mInput.capturing() && ImGui::Button( "Keep these" ) )
    {
      mInput.save( mInputPath );
    }

    ImGui::SeparatorText( "Gamepads" );
    auto const pads = mInput.gamepads();
    if ( pads.empty() )
    {
      ImGui::TextUnformatted( "None" );
    }
    for ( auto const& pad : pads )
    {
      ImGui::BulletText( "%s", pad.c_str() );
    }

    // The buttons held, lit.
    ImGui::SeparatorText( "Held" );
    std::uint16_t const held = mInput.buttons();
    for ( std::size_t i = 0; i < HostInput::BINDINGS; ++i )
    {
      HostInput::Binding const& binding = mInput.bindings( HostInput::Way::FLAT ).at( i );
      if ( i % 4 != 0 )
      {
        ImGui::SameLine();
      }
      ImGui::BeginDisabled( ( held & ( 1U << std::to_underlying( binding.button ) ) ) == 0 );
      ImGui::Button( std::string{ binding.name }.c_str(), ImVec2{ ImGui::GetFontSize() * 3.5F, 0.0F } );
      ImGui::EndDisabled();
    }
  }
  ImGui::End();
}

void Application::drawSoundWindow()
{
  if ( !mShowSound )
  {
    return;
  }
  if ( ImGui::Begin( "Sound", &mShowSound ) )
  {
    ImGui::Text( "Output: %s", mAudio ? "default device" : "none" );
    if ( mAudio )
    {
      ImGui::SameLine();
      ImGui::Text( "(%.0f ms queued)", mAudio->queuedSeconds() * 1000.0 );
    }
    auto const response = request( "io.get_state" );
    if ( response.at( "ok" ) != true )
    {
      ImGui::TextWrapped( "%s", response.at( "error" ).at( "message" ).get_ref<std::string const&>().c_str() );
      ImGui::End();
      return;
    }
    auto const& apu = response.at( "result" ).at( "apu" );
    ImGui::Text( "Master volume %u", apu.at( "master_volume" ).get<unsigned>() );
    ImGuiTableFlags const flags =
        ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingFixedFit;
    if ( ImGui::BeginTable( "channels", 5, flags ) )
    {
      for ( char const* heading : { "Channel", "On", "Frequency", "Left", "Right" } )
      {
        ImGui::TableSetupColumn( heading );
      }
      ImGui::TableHeadersRow();
      for ( char const* channel : { "ch1", "ch2", "ch3", "ch4" } )
      {
        auto const& state = apu.at( channel );
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted( channel );
        ImGui::TableNextColumn();
        ImGui::TextUnformatted( state.at( "enabled" ) == true ? "yes" : "-" );
        ImGui::TableNextColumn();
        ImGui::Text( "%03X", state.at( "frequency" ).get<unsigned>() );
        ImGui::TableNextColumn();
        ImGui::Text( "%u", state.at( "left_volume" ).get<unsigned>() );
        ImGui::TableNextColumn();
        ImGui::Text( "%u", state.at( "right_volume" ).get<unsigned>() );
      }
      ImGui::EndTable();
    }
    ImGui::Text( "Hyper Voice: %s", apu.at( "hyper_voice" ).at( "enabled" ) == true ? "on" : "off" );
  }
  ImGui::End();
}

void Application::renderFrame()
{
  ImDrawData* const drawData = ImGui::GetDrawData();
  bool const minimised = drawData->DisplaySize.x <= 0.0F || drawData->DisplaySize.y <= 0.0F;

  SDL_GPUCommandBuffer* const commands = SDL_AcquireGPUCommandBuffer( mDevice );
  if ( commands == nullptr )
  {
    return;
  }

  if ( mFrameChanged )
  {
    mScreen->upload( commands, mFrame );
    mFrameChanged = false;
  }
  if ( mScreenShown )
  {
    mRenderer->render( commands, *mScreen, mDisplay );
  }

  SDL_GPUTexture* swapchain = nullptr;
  if ( SDL_WaitAndAcquireGPUSwapchainTexture( commands, mWindow, &swapchain, nullptr, nullptr ) &&
       swapchain != nullptr && !minimised )
  {
    // The backend has to upload its vertices before the render pass begins.
    ImGui_ImplSDLGPU3_PrepareDrawData( drawData, commands );

    SDL_GPUColorTargetInfo target{};
    target.texture = swapchain;
    target.clear_color = SDL_FColor{ .r = 0.0F, .g = 0.0F, .b = 0.0F, .a = 1.0F };
    target.load_op = SDL_GPU_LOADOP_CLEAR;
    target.store_op = SDL_GPU_STOREOP_STORE;

    SDL_GPURenderPass* const pass = SDL_BeginGPURenderPass( commands, &target, 1, nullptr );
    ImGui_ImplSDLGPU3_RenderDrawData( drawData, commands, pass );
    SDL_EndGPURenderPass( pass );
  }
  SDL_SubmitGPUCommandBuffer( commands );
}

} // namespace ws::app
