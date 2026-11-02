// The horizontal and vertical blank timers: each counts down once a line or
// once a frame, interrupts as it leaves 1, and may reload itself. Ported from
// Mesen2's Core/WS/WsTimer.cpp at b9fa69ddc6d0a331fb103fdb5eef6904305703c2.

#include "Timer.hpp"

#include "MemoryManager.hpp"

namespace ws::machine
{

namespace
{

std::uint8_t lowByte( std::uint16_t value )
{
  return static_cast<std::uint8_t>( value );
}

std::uint8_t highByte( std::uint16_t value )
{
  return static_cast<std::uint8_t>( value >> 8U );
}

void setLowByte( std::uint16_t& dst, std::uint8_t value )
{
  dst = static_cast<std::uint16_t>( ( dst & 0xFF00U ) | value );
}

void setHighByte( std::uint16_t& dst, std::uint8_t value )
{
  dst = static_cast<std::uint16_t>( ( dst & 0x00FFU ) | ( static_cast<std::uint32_t>( value ) << 8U ) );
}

} // namespace

Timer::Timer( System& system ) : mSystem{ &system } {}

void Timer::tickHorizontalTimer()
{
  if ( mState.hTimer == 1 )
  {
    mSystem->getMemoryManager()->setIrqSource( IrqSource::HORIZONTAL_BLANK_TIMER );
  }

  if ( mState.hBlankEnabled && mState.hTimer != 0 )
  {
    --mState.hTimer;
    if ( mState.hTimer == 0 && mState.hBlankAutoReload )
    {
      mState.hTimer = mState.hReloadValue;
    }
  }
}

void Timer::tickVerticalTimer()
{
  if ( mState.vTimer == 1 )
  {
    mSystem->getMemoryManager()->setIrqSource( IrqSource::VERTICAL_BLANK_TIMER );
  }

  if ( mState.vBlankEnabled && mState.vTimer != 0 )
  {
    --mState.vTimer;
    if ( mState.vTimer == 0 && mState.vBlankAutoReload )
    {
      mState.vTimer = mState.vReloadValue;
    }
  }
}

std::uint8_t Timer::readPort( std::uint16_t port ) const
{
  switch ( port )
  {
  case 0xA2:
    return mState.control;
  case 0xA4:
    return lowByte( mState.hReloadValue );
  case 0xA5:
    return highByte( mState.hReloadValue );
  case 0xA6:
    return lowByte( mState.vReloadValue );
  case 0xA7:
    return highByte( mState.vReloadValue );
  case 0xA8:
    return lowByte( mState.hTimer );
  case 0xA9:
    return highByte( mState.hTimer );
  case 0xAA:
    return lowByte( mState.vTimer );
  case 0xAB:
    return highByte( mState.vTimer );
  default:
    return 0;
  }
}

void Timer::writePort( std::uint16_t port, std::uint8_t value )
{
  switch ( port )
  {
  case 0xA2:
    mState.control = value & 0x0FU;
    mState.hBlankEnabled = ( value & 0x01U ) != 0;
    mState.hBlankAutoReload = ( value & 0x02U ) != 0;
    mState.vBlankEnabled = ( value & 0x04U ) != 0;
    mState.vBlankAutoReload = ( value & 0x08U ) != 0;
    break;

  case 0xA4:
    setLowByte( mState.hReloadValue, value );
    mState.hTimer = mState.hReloadValue;
    break;

  case 0xA5:
    setHighByte( mState.hReloadValue, value );
    mState.hTimer = mState.hReloadValue;
    break;

  case 0xA6:
    setLowByte( mState.vReloadValue, value );
    mState.vTimer = mState.vReloadValue;
    break;

  case 0xA7:
    setHighByte( mState.vReloadValue, value );
    mState.vTimer = mState.vReloadValue;
    break;

  default:
    break;
  }
}

} // namespace ws::machine
