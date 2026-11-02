#pragma once

#include <SDL3/SDL_gpu.h>

#include <cstdint>
#include <span>

namespace ws::app
{

/// A GPU texture of RGBA pixels the interface shows: the emulated screen, or a
/// debugger's image. It owns the texture and the transfer buffer pictures are
/// staged through, so that showing one allocates nothing.
class GpuTexture
{
public:
  /// Creates both on `device`, `width` by `height`. Fails, with SDL's reason
  /// in SDL_GetError(), where the device cannot provide them.
  GpuTexture( SDL_GPUDevice* device, std::uint32_t width, std::uint32_t height );
  ~GpuTexture();

  GpuTexture( GpuTexture const& ) = delete;
  GpuTexture& operator=( GpuTexture const& ) = delete;
  GpuTexture( GpuTexture&& ) = delete;
  GpuTexture& operator=( GpuTexture&& ) = delete;

  [[nodiscard]] bool valid() const;

  /// Queues `rgba`, width by height RGBA pixels, to replace the texture's
  /// content when `commands` executes. Must be recorded outside any render
  /// pass.
  void upload( SDL_GPUCommandBuffer* commands, std::span<std::uint8_t const> rgba );

  [[nodiscard]] SDL_GPUTexture* texture() const;
  [[nodiscard]] std::uint32_t width() const;
  [[nodiscard]] std::uint32_t height() const;

private:
  SDL_GPUDevice* mDevice;
  std::uint32_t mWidth;
  std::uint32_t mHeight;
  SDL_GPUTexture* mTexture{};
  SDL_GPUTransferBuffer* mTransfer{};
};

} // namespace ws::app
