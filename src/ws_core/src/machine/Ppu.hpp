#pragma once

// The display: two background layers, sprites, windows, the LCD's icons.
// Ported from Mesen2's Core/WS/WsPpu.h and WsPpu.cpp at
// b9fa69ddc6d0a331fb103fdb5eef6904305703c2.

#include "System.hpp"

#include <array>
#include <cstdint>
#include <span>

namespace ws::machine
{

class Ppu
{
public:
  explicit Ppu( System& system );

  /// A dot: one CPU cycle.
  void exec()
  {
    if ( mState.scanline == SCREEN_HEIGHT )
    {
      processSpriteCopy();
    }

    if ( mState.cycle < 224 )
    {
      if ( mState.scanline < SCREEN_HEIGHT + 1 && mState.scanline > 0 )
      {
        // Palette lookup + output pixel on the first 224 cycles
        std::uint32_t const rowIndex = ( mState.scanline & 0x01U ) ^ 1U;
        PixelData const& data = mRowData[rowIndex][mState.cycle];
        std::uint32_t const screenWidth = mShowIcons ? mScreenWidth : SCREEN_WIDTH;
        std::uint32_t const offset = ( ( mState.scanline - 1U ) * screenWidth ) + mState.cycle;
        mOutputBuffers[mCurrentBuffer][offset] =
            data.priority == 0 ? getBgColor() : getPixelRgbColor( mState.mode, data.color, data.palette );
      }
    }
    else if ( mState.cycle == 224 )
    {
      processHblank();
    }
    else if ( mState.cycle == 255 )
    {
      // The next scanline starts at cycle 0.
      processEndOfScanline();
      return;
    }

    ++mState.cycle;
  }

  void setVideoMode( VideoMode mode );

  std::uint8_t readPort( std::uint16_t port );
  void writePort( std::uint16_t port, std::uint8_t value );

  std::uint8_t readLcdConfigPort( std::uint16_t port );
  void writeLcdConfigPort( std::uint16_t port, std::uint8_t value );

  [[nodiscard]] std::uint16_t getCycle() const;
  [[nodiscard]] std::uint16_t getScanline() const;
  [[nodiscard]] std::uint16_t getScanlineCount() const;
  [[nodiscard]] std::uint32_t getFrameCount() const;
  [[nodiscard]] std::uint16_t getScreenWidth() const;
  [[nodiscard]] std::uint16_t getScreenHeight() const;
  PpuState& getState();
  [[nodiscard]] PpuState const& getState() const;

  [[nodiscard]] std::uint16_t getVisibleScanlineCount() const;
  /// The picture being drawn, or the last one finished.
  [[nodiscard]] std::uint16_t const* getScreenBuffer( bool prevFrame ) const;

  /// Blanks what is left of the picture being drawn, for the debugger to show
  /// it as far as it got: getScreenBuffer( false ), getScreenWidth() by
  /// getScreenHeight().
  void debugSendFrame();
  void setOutputToBgColor();

  void showVolumeIcon();

  template <class Archive>
  void serialize( Archive& archive )
  {
    archive( mState );
    archive( mSpriteRam );
    archive( mRowData );
    archive( mScreenWidth );
    archive( mScreenHeight );
    archive( mShowIcons );
    archive( mCurrentBuffer );
    if constexpr ( Archive::READING )
    {
      mCurrentBuffer &= 0x01U;
    }
  }

private:
  struct PixelData
  {
    std::uint8_t palette{};
    std::uint8_t color{};
    std::uint8_t priority{};
  };

  using Icon = std::array<std::uint16_t, 11>;

  void processEndOfScanline();
  void processSpriteCopy();

  void drawIcons();
  void drawIcon( bool visible, Icon const& icon, std::uint8_t position );
  std::uint8_t getLcdStatus();

  void sendFrame();

  template <VideoMode MODE>
  void drawScanline();
  template <VideoMode MODE>
  void drawSprites();
  template <VideoMode MODE, int LAYER_INDEX>
  void drawBackground();

  template <VideoMode MODE>
  [[nodiscard]] std::uint8_t getPixelColor( std::uint16_t tileAddr, std::uint8_t column ) const;

  [[nodiscard]] std::uint16_t getBgColor() const
  {
    if ( mState.mode == VideoMode::MONOCHROME )
    {
      auto const bgBrightness = static_cast<std::uint16_t>( mState.bwShades[mState.bgColor & 0x07U] ^ 0x0FU );
      return static_cast<std::uint16_t>( bgBrightness | ( bgBrightness << 4U ) | ( bgBrightness << 8U ) );
    }
    std::uint32_t const paletteAddr = 0xFE00U | ( std::uint32_t{ mState.bgColor } << 1U );
    return static_cast<std::uint16_t>( mVram[paletteAddr] | ( ( mVram[paletteAddr | 1U] & 0x0FU ) << 8U ) );
  }

  [[nodiscard]] std::uint16_t getPixelRgbColor( VideoMode mode, std::uint8_t color, std::uint8_t palette ) const
  {
    switch ( mode )
    {
    case VideoMode::MONOCHROME:
    {
      std::uint8_t const shadeValue = mState.bwPalettes[( std::uint32_t{ palette } << 2U ) | color];
      auto const brightness = static_cast<std::uint16_t>( mState.bwShades[shadeValue] ^ 0x0FU );
      return static_cast<std::uint16_t>( brightness | ( brightness << 4U ) | ( brightness << 8U ) );
    }

    case VideoMode::COLOR_2BPP:
    case VideoMode::COLOR_4BPP:
    case VideoMode::COLOR_4BPP_PACKED:
    {
      std::uint32_t const paletteAddr = 0xFE00U | ( std::uint32_t{ palette } << 5U ) | ( std::uint32_t{ color } << 1U );
      return static_cast<std::uint16_t>( mVram[paletteAddr] | ( ( mVram[paletteAddr + 1] & 0x0FU ) << 8U ) );
    }
    }

    return 0;
  }

  void processHblank();

  [[nodiscard]] std::uint16_t* currentBuffer()
  {
    return mOutputBuffers[mCurrentBuffer].data();
  }

  System* mSystem;
  PpuState mState{};
  std::span<std::uint8_t> mVram;

  std::array<std::array<std::uint16_t, MAX_PIXEL_COUNT>, 2> mOutputBuffers{};
  std::uint8_t mCurrentBuffer{};

  std::array<std::uint8_t, 512> mSpriteRam{};
  std::array<std::array<PixelData, SCREEN_WIDTH>, 2> mRowData{};

  std::uint16_t mScreenHeight{};
  std::uint16_t mScreenWidth{};
  bool mShowIcons{};
};

} // namespace ws::machine
