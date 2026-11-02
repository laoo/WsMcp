add_executable( ws_app
  src/Application.cpp
  src/AudioOutput.cpp
  src/DebuggerWindows.cpp
  src/DisplaySettings.cpp
  src/EmulationThread.cpp
  src/GpuTexture.cpp
  src/HostInput.cpp
  src/Main.cpp
  src/ScreenRenderer.cpp )

set_target_properties( ws_app PROPERTIES OUTPUT_NAME wsmcp )

target_link_libraries( ws_app
  PRIVATE ws::server imgui SDL3::SDL3-static CLI11::CLI11 ws::warnings )
