#include "GpuTexture.hpp"

#include <cassert>
#include <cstring>

namespace ws::app
{

GpuTexture::GpuTexture( SDL_GPUDevice* device, std::uint32_t width, std::uint32_t height )
    : mDevice{ device }, mWidth{ width }, mHeight{ height }
{
  SDL_GPUTextureCreateInfo textureInfo{};
  textureInfo.type = SDL_GPU_TEXTURETYPE_2D;
  textureInfo.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
  textureInfo.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
  textureInfo.width = mWidth;
  textureInfo.height = mHeight;
  textureInfo.layer_count_or_depth = 1;
  textureInfo.num_levels = 1;
  mTexture = SDL_CreateGPUTexture( mDevice, &textureInfo );

  SDL_GPUTransferBufferCreateInfo transferInfo{};
  transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
  transferInfo.size = mWidth * mHeight * 4;
  mTransfer = SDL_CreateGPUTransferBuffer( mDevice, &transferInfo );
}

GpuTexture::~GpuTexture()
{
  if ( mTransfer != nullptr )
  {
    SDL_ReleaseGPUTransferBuffer( mDevice, mTransfer );
  }
  if ( mTexture != nullptr )
  {
    SDL_ReleaseGPUTexture( mDevice, mTexture );
  }
}

bool GpuTexture::valid() const
{
  return mTexture != nullptr && mTransfer != nullptr;
}

void GpuTexture::upload( SDL_GPUCommandBuffer* commands, std::span<std::uint8_t const> rgba )
{
  std::uint32_t const bytes = mWidth * mHeight * 4;
  assert( rgba.size() == bytes );

  // `cycle` lets the driver hand out a fresh buffer while the GPU may still be
  // reading the previous frame from this one, so the copy never waits on it.
  void* const staging = SDL_MapGPUTransferBuffer( mDevice, mTransfer, true );
  if ( staging == nullptr )
  {
    return;
  }
  std::memcpy( staging, rgba.data(), bytes );
  SDL_UnmapGPUTransferBuffer( mDevice, mTransfer );

  SDL_GPUTextureTransferInfo source{};
  source.transfer_buffer = mTransfer;
  source.pixels_per_row = mWidth;
  source.rows_per_layer = mHeight;

  SDL_GPUTextureRegion destination{};
  destination.texture = mTexture;
  destination.w = mWidth;
  destination.h = mHeight;
  destination.d = 1;

  SDL_GPUCopyPass* const copy = SDL_BeginGPUCopyPass( commands );
  SDL_UploadToGPUTexture( copy, &source, &destination, true );
  SDL_EndGPUCopyPass( copy );
}

SDL_GPUTexture* GpuTexture::texture() const
{
  return mTexture;
}

std::uint32_t GpuTexture::width() const
{
  return mWidth;
}

std::uint32_t GpuTexture::height() const
{
  return mHeight;
}

} // namespace ws::app
