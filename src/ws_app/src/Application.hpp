#pragma once

#include "DebuggerWindows.hpp"
#include "DisplaySettings.hpp"
#include "EmulationThread.hpp"
#include "GpuTexture.hpp"
#include "HostInput.hpp"
#include "ScreenRenderer.hpp"

#include "ws/Emulator.hpp"
#include "ws/control/Dispatcher.hpp"
#include "ws/server/McpHttpServer.hpp"
#include "ws/server/McpServer.hpp"
#include "ws/server/TcpLineServer.hpp"

#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL_video.h>

#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace ws::app
{

/// The desktop frontend: an SDL3 window rendered through SDL_GPU, with Dear
/// ImGui docked over it (docs/decisions/0013-the-desktop-application.md).
/// It reaches the emulator only through the dispatcher, as every other client
/// does.
class Application
{
public:
  /// Opens the window and the GPU device and sets up ImGui. Answers why when
  /// any of them cannot be had. What `settings` leave empty, the boot ROMs'
  /// directory, the saves' and the states', is the one chosen last, or one
  /// beside the application's settings.
  static std::expected<std::unique_ptr<Application>, std::string> create( Settings settings );

  /// Serves the control protocol to other programs while the window is open:
  /// JSON-lines on TCP `linePort`, MCP over HTTP on `mcpPort`, each unless 0.
  /// Their requests run on the emulation thread, on the machine on screen.
  /// Throws std::runtime_error when a port cannot be had.
  void serve( std::uint16_t linePort, std::uint16_t mcpPort );

  ~Application();

  Application( Application const& ) = delete;
  Application& operator=( Application const& ) = delete;
  Application( Application&& ) = delete;
  Application& operator=( Application&& ) = delete;

  /// Runs until the window is closed or Quit is chosen.
  void run();

private:
  Application( SDL_Window* window,
               SDL_GPUDevice* device,
               Settings settings,
               std::filesystem::path const& preferences,
               control::Json kept );

  /// Answers a request of the control protocol, on the emulation thread.
  control::Json request( std::string const& method, control::Json params = control::Json::object() );
  /// Keeps the error a response carries to show, or clears it; whether it
  /// succeeded.
  bool noteError( control::Json const& response );

  /// Hands the buttons to the emulation thread, and takes the last picture
  /// it completed.
  void updateEmulation();
  /// Makes the picture shown of the one taken: turned upright when the
  /// cartridge asks and the settings let it, and with what the LCD keeps of
  /// the one before.
  void present();
  /// Takes what the file dialogs chose, if they have.
  void takeChosen();

  void openCartridge();
  void chooseBiosDirectory();
  /// Puts the cartridge at `path` in, on the model chosen, and remembers it.
  void loadCartridge( std::filesystem::path const& path );
  /// Puts `path` first in File > Open recent.
  void rememberCartridge( std::filesystem::path const& path );
  /// Reads the boot ROMs from `directory`, and keeps it for the next start.
  void setBiosDirectory( std::filesystem::path const& directory );
  /// Saves the machine's state to, or loads it from, the cartridge's state
  /// file in the state directory.
  void saveState();
  void loadState();
  /// The state file of the cartridge running, or nothing without one.
  [[nodiscard]] std::optional<std::string> stateFile();
  void keepSetting( std::string const& key, control::Json value );

  /// Lays out one frame of the user interface.
  void drawInterface();
  void drawMenuBar();
  void drawEmulationMenu();
  void drawDisplayMenu();
  void drawScreenWindow();
  void drawStatusWindow();
  void drawInputWindow();
  void drawSoundWindow();

  /// Uploads what changed, then renders the interface into the swapchain.
  void renderFrame();

  SDL_Window* mWindow;
  SDL_GPUDevice* mDevice;
  /// Remade whenever the picture shown changes size: when it is turned.
  std::unique_ptr<GpuTexture> mScreen;
  /// The picture as the emulator drew it, and as shown.
  Picture mPicture;
  std::vector<std::uint8_t> mFrame;
  /// The picture shown, its red, green and blue unrounded: what the LCD keeps
  /// of the pictures before fades by less than a level a frame when ghosting
  /// is high, and would never fade at all in whole levels.
  std::vector<float> mGhost;
  std::uint32_t mFrameWidth{ machine::SCREEN_WIDTH };
  std::uint32_t mFrameHeight{ machine::SCREEN_HEIGHT };
  bool mFrameChanged{ true };
  /// The model cartridges are put in on; none to let each choose its own.
  std::optional<machine::Model> mModel;
  /// Where save states are kept.
  std::filesystem::path mStateDirectory;
  /// Null when no audio device could be opened: the emulation then runs
  /// silent, paced by the clock alone.
  std::unique_ptr<AudioOutput> mAudio;
  std::unique_ptr<EmulationThread> mEmulation;
  std::unique_ptr<server::TcpLineServer> mLineServer;
  std::unique_ptr<server::McpServer> mMcp;
  std::unique_ptr<server::McpHttpServer> mMcpHttp;
  std::unique_ptr<ScreenRenderer> mRenderer;
  HostInput mInput;
  std::unique_ptr<CpuWindow> mCpu;
  std::unique_ptr<MemoryWindow> mMemory;
  std::unique_ptr<IoWindow> mIo;
  std::unique_ptr<HistoryWindow> mHistory;
  bool mShowHistory{};
  bool mShowCpu{};
  bool mShowMemory{};
  bool mShowIo{};
  bool mShowSound{};
  /// Where the display settings, the key bindings and the application's own
  /// settings are kept, beside imgui.ini; empty when they are not kept.
  std::filesystem::path mDisplayPath;
  std::filesystem::path mInputPath;
  std::filesystem::path mSettingsPath;
  control::Json mSettings = control::Json::object();
  DisplaySettings mDisplay;
  /// Whether the screen was drawn into the renderer's target this frame, and
  /// whether it had the focus, which gives the console the keyboard.
  bool mScreenShown{};
  bool mScreenFocused{};
  std::string mImguiIniPath;
  bool mQuit{};
  bool mShowStatus{ true };
  bool mShowInput{};
  bool mPaused{};
  std::int64_t mPicturesShown{ -1 };
  std::string mLastError;
  /// What the dialogs chose, waiting to be taken: they answer on threads of
  /// their own.
  std::mutex mChosenMutex;
  std::optional<std::filesystem::path> mChosenCartridge;
  std::optional<std::filesystem::path> mChosenBiosDirectory;
};

} // namespace ws::app
