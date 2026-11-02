#include "Serial.hpp"

#include <utility>

namespace ws::machine
{

namespace
{

/// More than this many bytes sent and not read are let go, oldest first.
constexpr std::size_t MAX_SENT = 65536;

} // namespace

Serial::Serial( System& system ) : mSystem{ &system } {}

std::uint8_t Serial::read( std::uint16_t port )
{
  switch ( port )
  {
  case 0xB1:
    return mState.receiveBuffer;

  case 0xB3:
    updateState();
    return static_cast<std::uint8_t>( ( mState.hasReceiveData ? 0x01U : 0U ) | ( mState.receiveOverflow ? 0x02U : 0U ) |
                                      ( ( !mState.enabled || mState.hasSendData ) ? 0U : 0x04U ) |
                                      ( mState.highSpeed ? 0x40U : 0U ) | ( mState.enabled ? 0x80U : 0U ) );
  default:
    return 0;
  }
}

void Serial::write( std::uint16_t port, std::uint8_t value )
{
  switch ( port )
  {
  case 0xB1:
    mState.sendBuffer = value;
    mState.hasSendData = true;
    mState.sendClock = mSystem->getMasterClock();
    if ( mSent.size() == MAX_SENT )
    {
      mSent.erase( mSent.begin() );
    }
    mSent.push_back( value );
    break;

  case 0xB3:
    mState.enabled = ( value & 0x80U ) != 0;
    mState.highSpeed = ( value & 0x40U ) != 0;
    if ( ( value & 0x20U ) != 0 )
    {
      mState.receiveOverflow = false;
    }
    break;

  default:
    break;
  }
}

void Serial::updateState()
{
  if ( mState.hasSendData )
  {
    // At 9600 bauds/s, it takes 3200 master clocks to send 1 byte
    // At 38400 (high speed mode), it takes 800 master clocks
    std::uint64_t const cyclesPerByte = mState.highSpeed ? 800 : 3200;
    std::uint64_t const cyclesElapsed = mSystem->getMasterClock() - mState.sendClock;
    if ( cyclesElapsed > cyclesPerByte )
    {
      mState.hasSendData = false;
    }
  }
}

bool Serial::hasSendIrq()
{
  if ( mState.hasSendData )
  {
    updateState();
  }
  return mState.enabled && !mState.hasSendData;
}

std::vector<std::uint8_t> Serial::takeSent()
{
  return std::exchange( mSent, {} );
}

} // namespace ws::machine
