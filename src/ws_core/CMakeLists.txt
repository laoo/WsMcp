add_library( ws_core
  src/Emulator.cpp
  src/PortabilityChecks.cpp
  src/Version.cpp
  src/control/Dispatcher.cpp
  src/control/AudioMethods.cpp
  src/control/Batch.cpp
  src/control/CartMethods.cpp
  src/control/CpuMethods.cpp
  src/control/DebugMethods.cpp
  src/control/EmuMethods.cpp
  src/control/InputMethods.cpp
  src/control/IoMethods.cpp
  src/control/MemoryMethods.cpp
  src/control/Params.cpp
  src/control/RunMethods.cpp
  src/control/SerialMethods.cpp
  src/control/StateMethods.cpp
  src/control/VideoMethods.cpp
  src/cpu/Cpu.cpp
  src/cpu/CpuPrefetch.cpp
  src/cpu/Disassembler.cpp
  src/machine/Apu.cpp
  src/machine/Cart.cpp
  src/machine/ControlManager.cpp
  src/machine/DmaController.cpp
  src/machine/Eeprom.cpp
  src/machine/Flash.cpp
  src/machine/Machine.cpp
  src/machine/MemoryManager.cpp
  src/machine/Picture.cpp
  src/machine/Ppu.cpp
  src/machine/Rtc.cpp
  src/machine/Serial.cpp
  src/machine/System.cpp
  src/machine/Timer.cpp
  src/machine/TraceHistory.cpp )

add_library( ws::core ALIAS ws_core )

target_include_directories( ws_core PUBLIC include PRIVATE src )

target_compile_definitions( ws_core PRIVATE WS_VERSION="${WS_VERSION_STRING}" )

# nlohmann_json is PUBLIC because the dispatcher's interface is JSON: a request
# goes in as one and a response comes out as one, whichever transport carries
# them. spdlog is PUBLIC: every program built on the core logs through the one
# instance, and linking it twice is what Apple's linker warns about. miniz is
# PRIVATE: no header of the core includes it.
target_link_libraries( ws_core
  PUBLIC  nlohmann_json::nlohmann_json spdlog::spdlog
  PRIVATE ws::warnings miniz )
