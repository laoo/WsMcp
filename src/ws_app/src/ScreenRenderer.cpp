#include "ScreenRenderer.hpp"

#include <algorithm>
#include <span>

namespace ws::app
{

namespace
{

// The shaders as scripts/compile-shaders.sh compiles them, from
// src/ws_app/shaders: SPIR-V, and MSL source.
constexpr auto SCREEN_VERT_SPV = std::to_array<std::uint8_t>( {
#include "../shaders/compiled/screen_vert.spv.inc"
} );
constexpr auto SCREEN_VERT_MSL = std::to_array<std::uint8_t>( {
#include "../shaders/compiled/screen_vert.msl.inc"
} );
constexpr auto SHARP_FRAG_SPV = std::to_array<std::uint8_t>( {
#include "../shaders/compiled/sharp_frag.spv.inc"
} );
constexpr auto SHARP_FRAG_MSL = std::to_array<std::uint8_t>( {
#include "../shaders/compiled/sharp_frag.msl.inc"
} );
constexpr auto LCD_FRAG_SPV = std::to_array<std::uint8_t>( {
#include "../shaders/compiled/lcd_frag.spv.inc"
} );
constexpr auto LCD_FRAG_MSL = std::to_array<std::uint8_t>( {
#include "../shaders/compiled/lcd_frag.msl.inc"
} );

constexpr SDL_GPUTextureFormat TARGET_FORMAT = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;

/// A shader in each format the device might take.
struct ShaderCode
{
  std::span<std::uint8_t const> spirv;
  std::span<std::uint8_t const> msl;
};

/// The uniform block common.glsl declares.
struct Parameters
{
  std::array<float, 4> sourceSize;
  std::array<float, 4> outputSize;
  std::array<float, 4> settings;
};

SDL_GPUShader* createShader( SDL_GPUDevice* device, ShaderCode code, SDL_GPUShaderStage stage )
{
  bool const metal = ( SDL_GetGPUShaderFormats( device ) & SDL_GPU_SHADERFORMAT_MSL ) != 0;
  std::span<std::uint8_t const> const bytes = metal ? code.msl : code.spirv;
  SDL_GPUShaderCreateInfo info{};
  info.code = bytes.data();
  info.code_size = bytes.size();
  // SPIRV-Cross renames main, which MSL reserves.
  info.entrypoint = metal ? "main0" : "main";
  info.format = metal ? SDL_GPU_SHADERFORMAT_MSL : SDL_GPU_SHADERFORMAT_SPIRV;
  info.stage = stage;
  bool const fragment = stage == SDL_GPU_SHADERSTAGE_FRAGMENT;
  info.num_samplers = fragment ? 1 : 0;
  info.num_uniform_buffers = fragment ? 1 : 0;
  return SDL_CreateGPUShader( device, &info );
}

} // namespace

ScreenRenderer::ScreenRenderer( SDL_GPUDevice* device ) : mDevice{ device }
{
  SDL_GPUSamplerCreateInfo sampler{};
  sampler.min_filter = SDL_GPU_FILTER_LINEAR;
  sampler.mag_filter = SDL_GPU_FILTER_LINEAR;
  sampler.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
  sampler.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
  sampler.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
  sampler.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
  mSampler = SDL_CreateGPUSampler( device, &sampler );

  SDL_GPUShader* const vertex = createShader(
      device, ShaderCode{ .spirv = SCREEN_VERT_SPV, .msl = SCREEN_VERT_MSL }, SDL_GPU_SHADERSTAGE_VERTEX );
  std::array<ShaderCode, 2> const fragments{ ShaderCode{ .spirv = SHARP_FRAG_SPV, .msl = SHARP_FRAG_MSL },
                                             ShaderCode{ .spirv = LCD_FRAG_SPV, .msl = LCD_FRAG_MSL } };
  for ( std::size_t i = 0; i < fragments.size() && vertex != nullptr; ++i )
  {
    SDL_GPUShader* const fragment = createShader( device, fragments.at( i ), SDL_GPU_SHADERSTAGE_FRAGMENT );
    if ( fragment == nullptr )
    {
      break;
    }
    SDL_GPUColorTargetDescription target{};
    target.format = TARGET_FORMAT;
    SDL_GPUGraphicsPipelineCreateInfo info{};
    info.vertex_shader = vertex;
    info.fragment_shader = fragment;
    info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    info.target_info.color_target_descriptions = &target;
    info.target_info.num_color_targets = 1;
    mPipelines.at( i ) = SDL_CreateGPUGraphicsPipeline( device, &info );
    SDL_ReleaseGPUShader( device, fragment );
  }
  if ( vertex != nullptr )
  {
    SDL_ReleaseGPUShader( device, vertex );
  }
}

ScreenRenderer::~ScreenRenderer()
{
  for ( SDL_GPUGraphicsPipeline* const pipeline : mPipelines )
  {
    if ( pipeline != nullptr )
    {
      SDL_ReleaseGPUGraphicsPipeline( mDevice, pipeline );
    }
  }
  if ( mTarget != nullptr )
  {
    SDL_ReleaseGPUTexture( mDevice, mTarget );
  }
  if ( mSampler != nullptr )
  {
    SDL_ReleaseGPUSampler( mDevice, mSampler );
  }
}

bool ScreenRenderer::valid() const
{
  return mSampler != nullptr && std::ranges::all_of( mPipelines, []( auto* pipeline ) { return pipeline != nullptr; } );
}

SDL_GPUTexture* ScreenRenderer::target( std::uint32_t width, std::uint32_t height )
{
  if ( mTarget != nullptr && width == mWidth && height == mHeight )
  {
    return mTarget;
  }
  if ( mTarget != nullptr )
  {
    // SDL keeps it until the frames in flight that draw it are done.
    SDL_ReleaseGPUTexture( mDevice, mTarget );
    mTarget = nullptr;
  }
  SDL_GPUTextureCreateInfo info{};
  info.type = SDL_GPU_TEXTURETYPE_2D;
  info.format = TARGET_FORMAT;
  info.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
  info.width = std::max<std::uint32_t>( width, 1 );
  info.height = std::max<std::uint32_t>( height, 1 );
  info.layer_count_or_depth = 1;
  info.num_levels = 1;
  mTarget = SDL_CreateGPUTexture( mDevice, &info );
  mWidth = width;
  mHeight = height;
  return mTarget;
}

void ScreenRenderer::render( SDL_GPUCommandBuffer* commands, GpuTexture const& source, DisplaySettings const& settings )
{
  SDL_GPUGraphicsPipeline* const pipeline = mPipelines.at( static_cast<std::size_t>( settings.preset ) );
  if ( mTarget == nullptr || pipeline == nullptr )
  {
    return;
  }
  SDL_GPUColorTargetInfo target{};
  target.texture = mTarget;
  target.load_op = SDL_GPU_LOADOP_DONT_CARE;
  target.store_op = SDL_GPU_STOREOP_STORE;
  SDL_GPURenderPass* const pass = SDL_BeginGPURenderPass( commands, &target, 1, nullptr );
  SDL_BindGPUGraphicsPipeline( pass, pipeline );
  SDL_GPUTextureSamplerBinding const binding{ .texture = source.texture(), .sampler = mSampler };
  SDL_BindGPUFragmentSamplers( pass, 0, &binding, 1 );
  auto const sourceWidth = static_cast<float>( source.width() );
  auto const sourceHeight = static_cast<float>( source.height() );
  auto const width = static_cast<float>( mWidth );
  auto const height = static_cast<float>( mHeight );
  Parameters const parameters{
    .sourceSize = { sourceWidth, sourceHeight, 1.0F / sourceWidth, 1.0F / sourceHeight },
    .outputSize = { width, height, 1.0F / width, 1.0F / height },
    .settings = { settings.grid, 0.0F, 0.0F, 0.0F },
  };
  SDL_PushGPUFragmentUniformData( commands, 0, &parameters, sizeof( parameters ) );
  SDL_DrawGPUPrimitives( pass, 3, 1, 0, 0 );
  SDL_EndGPURenderPass( pass );
}

} // namespace ws::app
