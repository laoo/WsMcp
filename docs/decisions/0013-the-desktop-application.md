# 0013: The desktop application is SDL3 and Dear ImGui on SDL_GPU, with the WonderSwan held either way

**Status:** accepted

## Context

The owner asked for the desktop frontend their emulators share: SDL3, with Dear ImGui docked over
the emulated screen. It renders through SDL_GPU, because OpenGL is deprecated on macOS and
SDL_Renderer runs no shaders of our own. SDL_GPU compiles no shader source.

The WonderSwan asks three things of that frontend:

- Its screen is an LCD.
- A cartridge may ask to be held upright, by its header or by the LCD's icon. The console is then
  turned a quarter, and so are its pads.
- It has buttons, not a keyboard.

## Decision

- **The renderer is the shared one.** The application uses SDL_GPU with
  `imgui_impl_sdl3` and `imgui_impl_sdlgpu3`. The screen is an `R8G8B8A8_UNORM` texture, drawn
  through a preset's shader into a texture the size it is shown at, and shown with nearest
  sampling.
  - The shaders are written once in Vulkan GLSL. `scripts/compile-shaders.sh` compiles those in
    `src/ws_app/shaders` with glslang and SPIRV-Cross, to the SPIR-V and MSL committed under
    `src/ws_app/shaders/compiled`.
  - The device is created for SPIR-V, MSL and metallib.
- **The presets suit an LCD.**
  - Sharp draws each dot as a block of its colour, blending only a pixel's width at its edges.
  - LCD darkens the gaps between the dots once a dot is three pixels or more.
  - Ghosting blends some of the previous picture into each new one, as the LCD's slow dots do.
    It is done on the CPU, and is off by default.
  - There are no scanline or CRT presets, and no choice of dot shape: the LCD's dots are
    square.
- **An upright cartridge is shown upright.** The picture is turned a quarter anticlockwise, as
  Mesen2 turns it (`WsConsole::GetScreenRotationOverride`, 270°), and the Display menu can turn
  this off. Upright is `Machine::vertical()`: the header's flag, then the LCD's icons.
- **The keys and the pads turn with the console.**
  - The keyboard has a binding for each way the console is held, as Mesen2's
    `WsController` / `WsControllerVertical` have. The defaults are Mesen2's: flat, the X pad on
    the arrows and the Y pad on WASD; upright, the other way about, each turned. Start is on
    Return.
  - A gamepad is not rebindable. Its directions are turned with the console: flat, its pad and
    left stick are the X pad, its right stick the Y pad, east A and south B; upright, its pad and
    left stick are the Y pad, its face buttons and right stick the X pad, and its shoulders A and
    B.
- **What the window keeps, it keeps beside its settings**, in SDL's preference directory:
  - the layout, the display, the keys, the boot ROMs' directory, the recent cartridges and the
    history's length;
  - unless the command line names others, the saves (`saves/`) and the states (`states/`). A
    state is the cartridge's name with `.wsstate`.

## Consequences

- The application's code is the shared frontend's where the machines do not differ, and
  improvements can move between the owner's emulators.
- A shader is changed in its GLSL, compiled again with the script (`brew install glslang
  spirv-cross`), and both are committed together.
- The application's look and input are checked by running it. The suite links the core alone and
  has no window.
