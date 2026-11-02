// The display: scanlines drawn a mode at a time, sprites, windows, the
// registers' latches, the LCD's icons and the SwanCrystal's LCD config ports.
// Ported from Mesen2's Core/WS/WsPpu.cpp at
// b9fa69ddc6d0a331fb103fdb5eef6904305703c2.

#include "Ppu.hpp"

#include "Apu.hpp"
#include "ControlManager.hpp"
#include "MemoryManager.hpp"
#include "Timer.hpp"

#include <algorithm>
#include <cstddef>
#include <limits>

namespace ws::machine
{

Ppu::Ppu( System& system )
    : mSystem{ &system }, mVram{ system.getMemory( MemoryType::WORK_RAM ) }, mScreenHeight{ SCREEN_HEIGHT },
      mScreenWidth{ SCREEN_WIDTH }, mShowIcons{ system.getConfig().lcdShowIcons }
{
  mState.lastScanline = 158;
  if ( system.getModel() != Model::SWAN_CRYSTAL )
  {
    mState.backPorchScanline = 155;
  }
  mState.showVolumeIconFrame = std::numeric_limits<std::uint32_t>::max();

  if ( system.getModel() == Model::MONOCHROME )
  {
    mScreenHeight += 13;
  }
  else
  {
    mScreenWidth += 13;
  }
}

void Ppu::setVideoMode( VideoMode mode )
{
  mState.nextMode = mode;
}

void Ppu::processHblank()
{
  mSystem->getTimer()->tickHorizontalTimer();
  if ( mState.scanline < SCREEN_HEIGHT )
  {
    switch ( mState.mode )
    {
    case VideoMode::MONOCHROME:
      drawScanline<VideoMode::MONOCHROME>();
      break;
    case VideoMode::COLOR_2BPP:
      drawScanline<VideoMode::COLOR_2BPP>();
      break;
    case VideoMode::COLOR_4BPP:
      drawScanline<VideoMode::COLOR_4BPP>();
      break;
    case VideoMode::COLOR_4BPP_PACKED:
      drawScanline<VideoMode::COLOR_4BPP_PACKED>();
      break;
    }
  }
}

void Ppu::processEndOfScanline()
{
  mState.cycle = 0;
  mState.scanline++;

  mState.bgLayers[0].latch();
  mState.bgLayers[1].latch();
  mState.bgWindow.latch();
  mState.spriteWindow.latch();
  mState.spritesEnabledLatch = mState.spritesEnabled;
  mState.drawOutsideBgWindowLatch = mState.drawOutsideBgWindow;

  if ( mState.scanline > mState.lastScanline )
  {
    if ( mState.scanline <= 145 )
    {
      // Support sending frame to LCD even when number of scanlines is less than the 144px resolution
      sendFrame();
    }
    mState.mode = mState.nextMode;
    mState.scanline = 0;
    mCurrentBuffer ^= 0x01U;
    mShowIcons = mSystem->getConfig().lcdShowIcons;
  }
  else if ( mState.scanline == 145 )
  {
    sendFrame();
  }
  else if ( mState.scanline == 144 )
  {
    mSystem->getTimer()->tickVerticalTimer();
    mSystem->getControlManager()->triggerKeyIrq();
    mSystem->getMemoryManager()->setIrqSource( IrqSource::VERTICAL_BLANK );
  }

  if ( mState.scanline == mState.irqScanline )
  {
    mSystem->getMemoryManager()->setIrqSource( IrqSource::SCANLINE );
  }
}

template <VideoMode MODE>
void Ppu::drawScanline()
{
  std::uint32_t const rowIndex = mState.scanline & 0x01U;
  mRowData[rowIndex].fill( PixelData{} );

  drawSprites<MODE>();
  drawBackground<MODE, 0>();
  drawBackground<MODE, 1>();
}

template <VideoMode MODE>
void Ppu::drawSprites()
{
  if ( !mState.spritesEnabled )
  {
    return;
  }

  std::uint16_t const scanline = mState.scanline;
  std::uint32_t const rowIndex = mState.scanline & 0x01U;
  auto& row = mRowData[rowIndex];

  constexpr std::uint32_t tileSize = MODE >= VideoMode::COLOR_4BPP ? 32 : 16;
  constexpr std::uint32_t tileBytesPerRow = MODE >= VideoMode::COLOR_4BPP ? 4 : 2;
  constexpr std::uint32_t bank0Addr = MODE >= VideoMode::COLOR_4BPP ? 0x4000 : 0x2000;

  int spriteCount = 0;
  for ( std::uint32_t i = 0; i < mState.spriteCountLatch; i++ )
  {
    std::uint32_t const addr = i * 4;
    std::uint8_t const attributes = mSpriteRam[addr + 1];
    bool const highPriority = ( attributes & 0x20U ) != 0;

    std::uint8_t const y = mSpriteRam[addr + 2];
    auto tileRow = static_cast<std::uint8_t>( scanline - y );

    if ( tileRow < 8 && scanline < y + 8 )
    {
      spriteCount++;

      bool const showOutsideWindow = ( attributes & 0x10U ) != 0;
      bool const vMirror = ( attributes & 0x80U ) != 0;
      bool const hMirror = ( attributes & 0x40U ) != 0;

      std::uint32_t const tileIndex = mSpriteRam[addr] | ( ( attributes & 0x01U ) << 8U );
      auto const palette = static_cast<std::uint8_t>( ( ( attributes >> 1U ) & 0x07U ) + 8 );
      std::uint8_t const sprX = mSpriteRam[addr + 3];

      if ( vMirror )
      {
        tileRow = static_cast<std::uint8_t>( 7 - tileRow );
      }

      auto const tileDataAddr =
          static_cast<std::uint16_t>( bank0Addr + ( tileIndex * tileSize ) + ( tileRow * tileBytesPerRow ) );
      for ( std::uint32_t j = 0; j < 8; j++ )
      {
        auto const x = static_cast<std::uint8_t>( sprX + j );

        if ( x >= 224 )
        {
          continue;
        }
        if ( row[x].priority > 0 )
        {
          continue;
        }
        if ( mState.spriteWindow.enabledLatch &&
             showOutsideWindow == mState.spriteWindow.isInsideWindow( x, static_cast<std::uint8_t>( scanline ) ) )
        {
          // Don't draw this pixel, it's outside/inside the window and should only be drawn on the other side
          continue;
        }

        std::uint32_t tileColumn = j;
        if ( hMirror )
        {
          tileColumn = 7 - tileColumn;
        }

        std::uint8_t const color = getPixelColor<MODE>( tileDataAddr, static_cast<std::uint8_t>( tileColumn ) );
        if ( color != 0 || ( ( palette & 0x04U ) == 0 && MODE <= VideoMode::COLOR_2BPP ) )
        {
          row[x] = { .palette = palette,
                     .color = color,
                     .priority = static_cast<std::uint8_t>( highPriority ? 2 : 1 ) };
        }
      }
    }

    if ( spriteCount >= 32 )
    {
      break;
    }
  }
}

template <VideoMode MODE, int LAYER_INDEX>
void Ppu::drawBackground()
{
  constexpr std::uint32_t tileSize = MODE >= VideoMode::COLOR_4BPP ? 32 : 16;
  constexpr std::uint32_t tileBytesPerRow = MODE >= VideoMode::COLOR_4BPP ? 4 : 2;
  constexpr std::uint32_t bank0Addr = MODE >= VideoMode::COLOR_4BPP ? 0x4000 : 0x2000;
  constexpr std::uint32_t bank1Addr = MODE >= VideoMode::COLOR_4BPP ? 0x8000 : 0x4000;
  constexpr auto layerPriority = static_cast<std::uint8_t>( LAYER_INDEX + 1 );

  std::uint32_t const rowIndex = mState.scanline & 0x01U;
  auto& row = mRowData[rowIndex];
  BgLayer const& layer = mState.bgLayers[LAYER_INDEX];
  if ( !layer.enabledLatch )
  {
    return;
  }

  std::uint16_t const scanline = mState.scanline;
  std::uint32_t const layerAddr =
      layer.mapAddressLatch & ( mSystem->getModel() == Model::MONOCHROME ? 0x3FFFU : 0x7FFFU );

  for ( std::uint32_t cycle = 0; cycle < SCREEN_WIDTH; cycle++ )
  {
    std::uint32_t const y = ( scanline + layer.scrollYLatch ) & 0xFFU;
    std::uint32_t const x = ( cycle + layer.scrollXLatch ) & 0xFFU;
    std::uint32_t const tileMapRow = y / 8;
    std::uint32_t const column = x / 8;
    auto const tilemapAddr = static_cast<std::uint16_t>( layerAddr + ( tileMapRow * 32 * 2 ) + ( column * 2 ) );
    auto const tilemapData = static_cast<std::uint16_t>( mVram[tilemapAddr] | ( mVram[tilemapAddr + 1U] << 8U ) );

    std::uint32_t tileRow = y & 0x07U;
    std::uint32_t tileColumn = x & 0x07U;
    std::uint32_t const counter = 8 - tileColumn;

    std::uint32_t const tileIndex = tilemapData & 0x1FFU;
    auto const palette = static_cast<std::uint8_t>( ( tilemapData >> 9U ) & 0x0FU );
    bool const vMirror = ( tilemapData & 0x8000U ) != 0;
    bool const hMirror = ( tilemapData & 0x4000U ) != 0;
    if ( hMirror )
    {
      tileColumn = 7 - tileColumn;
    }
    if ( vMirror )
    {
      tileRow = 7 - tileRow;
    }

    std::uint32_t const tilesetAddr =
        MODE >= VideoMode::COLOR_2BPP && ( tilemapData & 0x2000U ) != 0 ? bank1Addr : bank0Addr;
    auto const tileDataAddr =
        static_cast<std::uint16_t>( tilesetAddr + ( tileIndex * tileSize ) + ( tileRow * tileBytesPerRow ) );

    for ( std::uint32_t i = cycle, end = std::min( cycle + counter, SCREEN_WIDTH ); i < end; i++ )
    {
      std::uint8_t const color = getPixelColor<MODE>( tileDataAddr, static_cast<std::uint8_t>( tileColumn ) );
      if ( hMirror )
      {
        tileColumn--;
      }
      else
      {
        tileColumn++;
      }

      if ( row[i].priority >= layerPriority )
      {
        continue;
      }

      if constexpr ( LAYER_INDEX == 1 )
      {
        if ( mState.bgWindow.enabledLatch &&
             mState.drawOutsideBgWindowLatch ==
                 mState.bgWindow.isInsideWindow( static_cast<std::uint8_t>( i ),
                                                 static_cast<std::uint8_t>( scanline ) ) )
        {
          // Pixel is hidden by window
          continue;
        }
      }

      // "In two bit per pixel modes: Palettes 0-3 and 8-11 are opaque. For these, index zero is treated as opaque."
      if ( color != 0 || ( ( palette & 0x04U ) == 0 && MODE <= VideoMode::COLOR_2BPP ) )
      {
        row[i] = { .palette = palette, .color = color, .priority = layerPriority };
      }
    }

    cycle += counter - 1;
  }
}

template <VideoMode MODE>
std::uint8_t Ppu::getPixelColor( std::uint16_t tileAddr, std::uint8_t column ) const
{
  if constexpr ( MODE == VideoMode::COLOR_4BPP_PACKED )
  {
    return static_cast<std::uint8_t>( ( mVram[tileAddr + ( column / 2U )] >> ( ( column & 0x01U ) != 0 ? 0U : 4U ) ) &
                                      0x0FU );
  }
  else
  {
    auto const bit = [&]( std::uint32_t offset, std::uint32_t shift )
    { return ( ( static_cast<std::uint32_t>( mVram[tileAddr + offset] << column ) & 0x80U ) >> shift ); };
    std::uint32_t color = bit( 0, 7 ) | bit( 1, 6 );
    if constexpr ( MODE == VideoMode::COLOR_4BPP )
    {
      color |= bit( 2, 5 ) | bit( 3, 4 );
    }
    return static_cast<std::uint8_t>( color );
  }
}

void Ppu::processSpriteCopy()
{
  if ( mState.cycle == 0 )
  {
    mState.spriteCountLatch = mState.spriteCount;
  }

  std::uint32_t const baseAddr =
      mState.spriteTableAddress & ( mSystem->getModel() == Model::MONOCHROME ? 0x3FFFU : 0x7FFFU );

  std::uint32_t const i = std::uint32_t{ mState.cycle } << 1U;
  std::uint32_t const first = std::uint32_t{ mState.firstSpriteIndex } * 4;
  mSpriteRam[i] = mVram[baseAddr + ( ( first + i ) & 0x1FFU )];
  mSpriteRam[i + 1] = mVram[baseAddr + ( ( first + i + 1 ) & 0x1FFU )];
}

void Ppu::drawIcons()
{
  // 11x11 1bpp icons (same shapes as ares)
  static constexpr Icon POWER{ 0x70, 0xD8, 0x18C, 0x18C, 0x3DE, 0x3FE, 0x3FE, 0x1FC, 0x1FC, 0xF8, 0x70 };
  static constexpr Icon SLEEP{ 0x80, 0x250, 0x11B, 0x1F, 0x3E, 0xFC, 0x3C, 0x1E, 0x3E, 0x33, 0 };
  static constexpr Icon VOLUME_WS_OFF{ 0, 0x10, 0x18, 0x1C, 0x1E, 0x1E, 0x1E, 0x1C, 0x18, 0x10, 0 };
  static constexpr Icon VOLUME_WS_LOW{ 0, 0x10, 0x18, 0x5C, 0x9E, 0x9E, 0x9E, 0x5C, 0x18, 0x10, 0 };
  static constexpr Icon VOLUME_WS_HIGH{ 0, 0x90, 0x118, 0x25C, 0x29E, 0x29E, 0x29E, 0x25C, 0x118, 0x90, 0 };
  static constexpr Icon VOLUME_WSC_OFF{ 0, 0, 0, 0, 0, 0, 0, 0x3FE, 0x1FC, 0xF8, 0x70 };
  static constexpr Icon VOLUME_WSC_LOW{ 0, 0, 0, 0xF8, 0x104, 0, 0, 0x3FE, 0x1FC, 0xF8, 0x70 };
  static constexpr Icon VOLUME_WSC_MED{ 0x1FC, 0x202, 0x401, 0, 0x70, 0x88, 0, 0x3FE, 0x1FC, 0xF8, 0x70 };
  static constexpr Icon VOLUME_WSC_HIGH{ 0x1FC, 0x202, 0x4F9, 0x104, 0x272, 0x88, 0, 0x3FE, 0x1FC, 0xF8, 0x70 };
  static constexpr Icon HEADPHONES{ 0x70, 0x18C, 0x306, 0x20B, 0x41F, 0x40E, 0x447, 0x2E2, 0x370, 0x1F8, 0xD0 };
  static constexpr Icon VERTICAL_ICON{ 0x6, 0x1E, 0x38, 0x3BF, 0x7FF, 0x7FF, 0x7FF, 0x3BF, 0x38, 0x1E, 0x6 };
  static constexpr Icon HORIZONTAL_ICON{ 0x70, 0xF8, 0xF8, 0xF8, 0x70, 0x1FC, 0x3FE, 0x3FE, 0x6FB, 0x6FB, 0xF8 };
  static constexpr Icon AUX3{ 0, 0x70, 0x1FC, 0x1FC, 0x3FE, 0x3FE, 0x3FE, 0x1FC, 0x1FC, 0x70, 0 };
  static constexpr Icon AUX2{ 0, 0, 0x70, 0xF8, 0x1FC, 0x1FC, 0x1FC, 0xF8, 0x70, 0, 0 };
  static constexpr Icon AUX1{ 0, 0, 0, 0, 0x70, 0x70, 0x70, 0, 0, 0, 0 };

  auto& buffer = mOutputBuffers[mCurrentBuffer];
  bool const monochrome = mSystem->getModel() == Model::MONOCHROME;
  if ( monochrome )
  {
    auto const start = buffer.begin() + ( std::size_t{ SCREEN_WIDTH } * SCREEN_HEIGHT );
    std::fill( start, start + ( std::size_t{ SCREEN_WIDTH } * 13 ), std::uint16_t{ 0xFFF } );
  }
  else
  {
    for ( std::uint32_t i = 0; i < SCREEN_HEIGHT; i++ )
    {
      auto const start =
          buffer.begin() + static_cast<std::ptrdiff_t>( ( std::size_t{ i } * mScreenWidth ) + SCREEN_WIDTH );
      std::fill( start, start + 13, std::uint16_t{ 0 } );
    }
  }

  drawIcon( true, POWER, 0 );
  drawIcon( mState.icons.sleep, SLEEP, 26 );
  if ( mState.showVolumeIconFrame <= mState.frameCount && mState.frameCount - mState.showVolumeIconFrame < 128 )
  {
    // Show speaker/headphone icons if sound button was pressed within the last 128 frames
    if ( mSystem->getConfig().audioMode == AudioMode::HEADPHONES )
    {
      drawIcon( true, HEADPHONES, 65 );
    }
    else if ( monochrome )
    {
      switch ( mSystem->getApu()->getMasterVolume() )
      {
      default:
      case 0:
        drawIcon( true, VOLUME_WS_OFF, 52 );
        break;
      case 1:
        drawIcon( true, VOLUME_WS_LOW, 52 );
        break;
      case 2:
        drawIcon( true, VOLUME_WS_HIGH, 52 );
        break;
      }
    }
    else
    {
      switch ( mSystem->getApu()->getMasterVolume() )
      {
      default:
      case 0:
        drawIcon( true, VOLUME_WSC_OFF, 52 );
        break;
      case 1:
        drawIcon( true, VOLUME_WSC_LOW, 52 );
        break;
      case 2:
        drawIcon( true, VOLUME_WSC_MED, 52 );
        break;
      case 3:
        drawIcon( true, VOLUME_WSC_HIGH, 52 );
        break;
      }
    }
  }

  drawIcon( mState.icons.vertical, VERTICAL_ICON, 78 );
  drawIcon( mState.icons.horizontal, HORIZONTAL_ICON, 91 );
  drawIcon( mState.icons.aux3, AUX3, 104 );
  drawIcon( mState.icons.aux2, AUX2, 117 );
  drawIcon( mState.icons.aux1, AUX1, 130 );
}

void Ppu::drawIcon( bool visible, Icon const& icon, std::uint8_t position )
{
  if ( !visible )
  {
    return;
  }

  std::uint32_t xPos = 0;
  std::uint32_t yPos = 0;
  std::uint16_t color = 0;
  if ( mSystem->getModel() == Model::MONOCHROME )
  {
    xPos = position + 1U;
    yPos = 145;
    color = 0;
  }
  else
  {
    xPos = 225;
    yPos = SCREEN_HEIGHT - 13 - position;
    color = 0xFFF;
  }

  auto& buffer = mOutputBuffers[mCurrentBuffer];
  for ( std::uint32_t y = 0; y < 11; y++ )
  {
    std::uint32_t const basePos = ( ( yPos + y ) * mScreenWidth ) + xPos;
    for ( std::uint32_t x = 0; x < 11; x++ )
    {
      if ( ( icon[y] & ( 1U << x ) ) != 0 )
      {
        buffer[basePos + x] = color;
      }
    }
  }
}

std::uint16_t Ppu::getCycle() const
{
  return mState.cycle;
}

std::uint16_t Ppu::getScanline() const
{
  return mState.scanline;
}

std::uint16_t Ppu::getScanlineCount() const
{
  return static_cast<std::uint16_t>( mState.lastScanline + 1 );
}

std::uint32_t Ppu::getFrameCount() const
{
  return mState.frameCount;
}

std::uint16_t Ppu::getScreenWidth() const
{
  return mShowIcons ? mScreenWidth : static_cast<std::uint16_t>( SCREEN_WIDTH );
}

std::uint16_t Ppu::getScreenHeight() const
{
  return mShowIcons ? mScreenHeight : static_cast<std::uint16_t>( SCREEN_HEIGHT );
}

PpuState& Ppu::getState()
{
  return mState;
}

PpuState const& Ppu::getState() const
{
  return mState;
}

std::uint16_t Ppu::getVisibleScanlineCount() const
{
  std::uint16_t const scanlineCount = getScanlineCount();
  return scanlineCount <= SCREEN_HEIGHT ? static_cast<std::uint16_t>( scanlineCount - 1 )
                                        : static_cast<std::uint16_t>( SCREEN_HEIGHT );
}

std::uint16_t const* Ppu::getScreenBuffer( bool prevFrame ) const
{
  return mOutputBuffers[prevFrame ? mCurrentBuffer ^ 0x01U : mCurrentBuffer].data();
}

void Ppu::debugSendFrame()
{
  int const offset = std::max( 0, mState.cycle + ( ( mState.scanline - 1 ) * mScreenWidth ) );
  auto& buffer = mOutputBuffers[mCurrentBuffer];
  if ( static_cast<std::size_t>( offset ) < buffer.size() )
  {
    std::fill( buffer.begin() + offset, buffer.end(), std::uint16_t{ 0 } );
  }
}

void Ppu::setOutputToBgColor()
{
  mOutputBuffers[mCurrentBuffer].fill( getBgColor() );
}

void Ppu::showVolumeIcon()
{
  mState.showVolumeIconFrame = mState.frameCount;
}

std::uint8_t Ppu::getLcdStatus()
{
  std::uint8_t const masterVolume = mSystem->getApu()->getMasterVolume();
  std::uint32_t volumeLevel = 0;

  if ( mSystem->getModel() == Model::MONOCHROME )
  {
    switch ( masterVolume )
    {
    default:
    case 0:
      volumeLevel = 0;
      break;
    case 1:
      volumeLevel = 2;
      break;
    case 2:
      volumeLevel = 3;
      break;
    }
  }
  else
  {
    switch ( masterVolume )
    {
    default:
    case 0:
      volumeLevel = 0;
      break;
    case 1:
      volumeLevel = 2;
      break;
    case 2:
      volumeLevel = 1;
      break;
    case 3:
      volumeLevel = 3;
      break;
    }
  }

  bool headphone = false;
  bool speaker = false;
  if ( mState.showVolumeIconFrame <= mState.frameCount && mState.frameCount - mState.showVolumeIconFrame < 128 )
  {
    // Show speaker/headphone icons if sound button was pressed within the last 128 frames
    if ( mSystem->getConfig().audioMode == AudioMode::HEADPHONES )
    {
      headphone = true;
    }
    else
    {
      speaker = true;
    }
  }

  return static_cast<std::uint8_t>( ( mState.sleepEnabled ? 0x01U : 0U ) | ( headphone ? 0x02U : 0U ) |
                                    ( volumeLevel << 2U ) | ( speaker ? 0x10U : 0U ) );
}

void Ppu::sendFrame()
{
  auto& buffer = mOutputBuffers[mCurrentBuffer];
  if ( mState.sleepEnabled || !mState.lcdEnabled || mState.lastScanline == 255 || mSystem->isPowerOff() )
  {
    // Screen should be white when in sleep mode, or if the last scanline is set to 255
    buffer.fill( 0xFFF );
  }
  else if ( mState.lastScanline < 144 )
  {
    // Clear everything after the last scanline (results in less than 144 visible scanlines)
    std::fill( buffer.begin() + static_cast<std::ptrdiff_t>( std::size_t{ mState.lastScanline } * mScreenWidth ),
               buffer.end(),
               std::uint16_t{ 0xFFF } );
  }

  if ( mShowIcons )
  {
    drawIcons();
  }

  mState.frameCount++;

  std::uint32_t const width = getScreenWidth();
  std::uint32_t const height = getScreenHeight();
  mSystem->frameDone( std::span<std::uint16_t const>{ buffer.data(), std::size_t{ width } * height }, width, height );
}

std::uint8_t Ppu::readPort( std::uint16_t port )
{
  bool const colorEnabled = mSystem->getMemoryManager()->isColorEnabled();
  switch ( port )
  {
  case 0x00:
    return mState.control;
  case 0x01:
    return static_cast<std::uint8_t>( mState.bgColor & ( colorEnabled ? 0xFFU : 0x07U ) );
  case 0x02:
    return static_cast<std::uint8_t>( mState.scanline );
  case 0x03:
    return mState.irqScanline;
  case 0x04:
    return static_cast<std::uint8_t>( ( mState.spriteTableAddress >> 9U ) & ( colorEnabled ? 0x3FU : 0x1FU ) );
  case 0x05:
    return mState.firstSpriteIndex;
  case 0x06:
    return mState.spriteCount;
  case 0x07:
    return static_cast<std::uint8_t>( mState.screenAddress & ( colorEnabled ? 0xFFU : 0x77U ) );
  case 0x08:
    return mState.bgWindow.left;
  case 0x09:
    return mState.bgWindow.top;
  case 0x0A:
    return mState.bgWindow.right;
  case 0x0B:
    return mState.bgWindow.bottom;

  case 0x0C:
    return mState.spriteWindow.left;
  case 0x0D:
    return mState.spriteWindow.top;
  case 0x0E:
    return mState.spriteWindow.right;
  case 0x0F:
    return mState.spriteWindow.bottom;

  case 0x10:
    return mState.bgLayers[0].scrollX;
  case 0x11:
    return mState.bgLayers[0].scrollY;
  case 0x12:
    return mState.bgLayers[1].scrollX;
  case 0x13:
    return mState.bgLayers[1].scrollY;
  case 0x14:
    return mState.lcdControl;
  case 0x15:
    return mState.icons.value;
  case 0x16:
    return mState.lastScanline;
  case 0x17:
    return mState.backPorchScanline;
  case 0x1A:
    return getLcdStatus();
  case 0x1B:
    return 0;

  case 0x1C:
  case 0x1D:
  case 0x1E:
  case 0x1F:
  {
    std::size_t const index = std::size_t{ port - 0x1CU } * 2;
    return static_cast<std::uint8_t>( mState.bwShades[index] | ( mState.bwShades[index + 1] << 4U ) );
  }

  default:
    if ( port >= 0x20 && port <= 0x3F )
    {
      std::size_t const index = std::size_t{ port - 0x20U } * 2;
      return static_cast<std::uint8_t>( mState.bwPalettes[index] | ( mState.bwPalettes[index + 1] << 4U ) );
    }
    return mSystem->getMemoryManager()->getUnmappedPort();
  }
}

void Ppu::writePort( std::uint16_t port, std::uint8_t value )
{
  bool const monochrome = mSystem->getModel() == Model::MONOCHROME;
  switch ( port )
  {
  case 0x00:
    mState.control = value & 0x3FU;
    mState.bgLayers[0].enabled = ( value & 0x01U ) != 0;
    mState.bgLayers[1].enabled = ( value & 0x02U ) != 0;
    mState.spritesEnabled = ( value & 0x04U ) != 0;
    mState.spriteWindow.enabled = ( value & 0x08U ) != 0;
    mState.drawOutsideBgWindow = ( value & 0x10U ) != 0;
    mState.bgWindow.enabled = ( value & 0x20U ) != 0;
    break;

  case 0x01:
    mState.bgColor = static_cast<std::uint8_t>( value & ( monochrome ? 0x07U : 0xFFU ) );
    break;
  case 0x03:
    mState.irqScanline = value;
    break;
  case 0x04:
    mState.spriteTableAddress = static_cast<std::uint16_t>( ( value & ( monochrome ? 0x1FU : 0x3FU ) ) << 9U );
    break;

  case 0x05:
    mState.firstSpriteIndex = value & 0x7FU;
    break;
  case 0x06:
    mState.spriteCount = value;
    break;

  case 0x07:
    mState.screenAddress = static_cast<std::uint8_t>( value & ( monochrome ? 0x77U : 0xFFU ) );
    mState.bgLayers[0].mapAddress = static_cast<std::uint16_t>( ( mState.screenAddress & 0x0FU ) << 11U );
    mState.bgLayers[1].mapAddress = static_cast<std::uint16_t>( ( mState.screenAddress & 0xF0U ) << 7U );
    break;

  case 0x08:
    mState.bgWindow.left = value;
    break;
  case 0x09:
    mState.bgWindow.top = value;
    break;
  case 0x0A:
    mState.bgWindow.right = value;
    break;
  case 0x0B:
    mState.bgWindow.bottom = value;
    break;

  case 0x0C:
    mState.spriteWindow.left = value;
    break;
  case 0x0D:
    mState.spriteWindow.top = value;
    break;
  case 0x0E:
    mState.spriteWindow.right = value;
    break;
  case 0x0F:
    mState.spriteWindow.bottom = value;
    break;

  case 0x10:
    mState.bgLayers[0].scrollX = value;
    break;
  case 0x11:
    mState.bgLayers[0].scrollY = value;
    break;
  case 0x12:
    mState.bgLayers[1].scrollX = value;
    break;
  case 0x13:
    mState.bgLayers[1].scrollY = value;
    break;

  case 0x14:
    mState.lcdEnabled = ( value & 0x01U ) != 0;
    switch ( mSystem->getModel() )
    {
    case Model::SWAN_CRYSTAL:
      mState.lcdControl = value & 0x01U;
      break;

    case Model::COLOR:
      mState.highContrast = ( value & 0x02U ) != 0;
      mState.lcdControl = value & 0xF3U;
      break;

    default:
      mState.lcdControl = value & 0xF1U;
      break;
    }
    break;

  case 0x15:
    mState.icons.sleep = ( value & 0x01U ) != 0;
    mState.icons.vertical = ( value & 0x02U ) != 0;
    mState.icons.horizontal = ( value & 0x04U ) != 0;
    mState.icons.aux1 = ( value & 0x08U ) != 0;
    mState.icons.aux2 = ( value & 0x10U ) != 0;
    mState.icons.aux3 = ( value & 0x20U ) != 0;
    mState.icons.value = value & 0x3FU;
    break;

  case 0x16:
    mState.lastScanline = value;
    break;

  case 0x17:
    if ( mSystem->getModel() != Model::SWAN_CRYSTAL )
    {
      mState.backPorchScanline = value;
    }
    break;

  case 0x1A:
    mState.sleepEnabled = ( value & 0x01U ) != 0;
    break;

  case 0x1C:
  case 0x1D:
  case 0x1E:
  case 0x1F:
  {
    std::size_t const index = std::size_t{ port - 0x1CU } * 2;
    mState.bwShades[index] = value & 0x0FU;
    mState.bwShades[index + 1] = static_cast<std::uint8_t>( ( value >> 4U ) & 0x0FU );
    break;
  }

  default:
    if ( port >= 0x20 && port <= 0x3F )
    {
      std::size_t const index = port - 0x20U;

      // Color 0 only exists on palettes 0-3 and 8-11
      // For other palettes, this is always read as 0 (writes have no effect)
      if ( ( index & 0x09U ) != 0x08 )
      {
        mState.bwPalettes[index * 2] = value & 0x07U;
      }
      mState.bwPalettes[( index * 2 ) + 1] = static_cast<std::uint8_t>( ( value >> 4U ) & 0x07U );
    }
    break;
  }
}

std::uint8_t Ppu::readLcdConfigPort( std::uint16_t port )
{
  return mState.lcdTftConfig[port - 0x70U];
}

void Ppu::writeLcdConfigPort( std::uint16_t port, std::uint8_t value )
{
  mState.lcdTftConfig[port - 0x70U] = value;
}

} // namespace ws::machine
