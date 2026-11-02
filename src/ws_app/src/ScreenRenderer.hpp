#pragma once

#include "DisplaySettings.hpp"
#include "GpuTexture.hpp"

#include <SDL3/SDL_gpu.h>

#include <array>
#include <cstdint>

namespace ws::app
{

/// Draws the emulated screen through a preset's shader into a texture the
/// size it is shown at, in the display's pixels
/// (docs/decisions/0013-the-desktop-application.md).
class ScreenRenderer
{
public:
  /// Creates the presets' pipelines on `device`. Fails, with SDL's reason in
  /// SDL_GetError(), where the device cannot have them.
  explicit ScreenRenderer( SDL_GPUDevice* device );
  ~ScreenRenderer();

  ScreenRenderer( ScreenRenderer const& ) = delete;
  ScreenRenderer& operator=( ScreenRenderer const& ) = delete;
  ScreenRenderer( ScreenRenderer&& ) = delete;
  ScreenRenderer& operator=( ScreenRenderer&& ) = delete;

  [[nodiscard]] bool valid() const;

  /// The texture the screen is drawn into, `width` by `height` pixels, made
  /// anew when the size changes; null if it cannot be.
  SDL_GPUTexture* target( std::uint32_t width, std::uint32_t height );

  /// Records the pass that draws `source` into the target with `settings`.
  /// Must be recorded outside any render pass; does nothing before target()
  /// has made one.
  void render( SDL_GPUCommandBuffer* commands, GpuTexture const& source, DisplaySettings const& settings );

private:
  SDL_GPUDevice* mDevice;
  SDL_GPUSampler* mSampler{};
  std::array<SDL_GPUGraphicsPipeline*, 2> mPipelines{};
  SDL_GPUTexture* mTarget{};
  std::uint32_t mWidth{};
  std::uint32_t mHeight{};
};

} // namespace ws::app
