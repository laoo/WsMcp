#include "Cart.hpp"

#include "Eeprom.hpp"
#include "MemoryManager.hpp"

#include <utility>

namespace ws::machine
{

namespace
{

/// The 2003's banks are ten bits wide, the top two at $D1, $D3 and $D5.
constexpr std::uint16_t HIGH_BANK_BITS = 0x0300;

std::uint16_t withLow( std::uint16_t bank, std::uint8_t low )
{
  return static_cast<std::uint16_t>( ( bank & 0xFF00U ) | low );
}

} // namespace

Cart::Cart( System& system, bool bandai2003, bool flash ) : mSystem{ &system }
{
  mState.selectedBanks = { 0xFF, 0xFF, 0xFF, 0xFF };
  if ( bandai2003 )
  {
    mRtc.emplace( system, mState.rtc );
    mRtc->powerOn();
  }
  if ( flash )
  {
    mFlash.emplace( system, mState.flash );
  }
}

Eeprom* Cart::getEeprom() const
{
  return mSystem->getCartEeprom();
}

Rtc* Cart::getRtc()
{
  return mRtc ? &*mRtc : nullptr;
}

Flash* Cart::getFlash()
{
  return mFlash ? &*mFlash : nullptr;
}

std::uint32_t Cart::romOffset( std::uint32_t address ) const
{
  auto const& banks = mState.selectedBanks;
  switch ( address >> 16U )
  {
  case 0x1:
    return ( banks[1] * 0x10000U ) | ( address & 0xFFFFU );
  case 0x2:
    return ( banks[2] * 0x10000U ) | ( address & 0xFFFFU );
  case 0x3:
    return ( banks[3] * 0x10000U ) | ( address & 0xFFFFU );
  default:
    return ( banks[0] * 0x100000U ) | ( address & 0xFFFFFU );
  }
}

std::uint8_t Cart::readFlash( std::uint32_t address )
{
  return mFlash ? mFlash->read( romOffset( address ) ) : mSystem->getMemoryManager()->getUnmappedPort();
}

void Cart::writeFlash( std::uint32_t address, std::uint8_t value )
{
  if ( mFlash )
  {
    mFlash->write( romOffset( address ), value );
  }
}

std::uint8_t Cart::peekFlash( std::uint32_t address ) const
{
  std::span<std::uint8_t const> const rom = std::as_const( *mSystem ).getMemory( MemoryType::PRG_ROM );
  return rom.empty() ? 0 : rom[romOffset( address ) % rom.size()];
}

void Cart::refreshMappings()
{
  MemoryManager* const memoryManager = mSystem->getMemoryManager();
  // The 2003 can put the ROM where the SRAM is, to be written; a mask ROM
  // takes no write, a flash chip takes them as its commands.
  if ( mState.flashSelected )
  {
    memoryManager->map( 0x10000, 0x1FFFF, MemoryType::PRG_ROM, mState.selectedBanks[1] * 0x10000U, true );
  }
  else
  {
    memoryManager->map( 0x10000, 0x1FFFF, MemoryType::CART_RAM, mState.selectedBanks[1] * 0x10000U, false );
  }
  memoryManager->map( 0x20000, 0x2FFFF, MemoryType::PRG_ROM, mState.selectedBanks[2] * 0x10000U, true );
  memoryManager->map( 0x30000, 0x3FFFF, MemoryType::PRG_ROM, mState.selectedBanks[3] * 0x10000U, true );
  memoryManager->map( 0x40000, 0xFFFFF, MemoryType::PRG_ROM, ( mState.selectedBanks[0] * 0x100000U ) + 0x40000U, true );
  if ( !mFlash )
  {
    return;
  }
  // While the chip answers commands rather than its contents, every read of
  // the ROM asks it.
  if ( !mFlash->readsArray() )
  {
    memoryManager->unmap( 0x20000, 0xFFFFF );
    memoryManager->mapToFlash( 0x20000, 0xFFFFF, true, false );
  }
  if ( mState.flashSelected )
  {
    if ( !mFlash->readsArray() )
    {
      memoryManager->unmap( 0x10000, 0x1FFFF );
    }
    memoryManager->mapToFlash( 0x10000, 0x1FFFF, !mFlash->readsArray(), true );
  }
}

std::uint8_t Cart::readPort( std::uint16_t port )
{
  if ( port < 0xC4 )
  {
    return static_cast<std::uint8_t>( mState.selectedBanks[port - 0xC0U] );
  }
  if ( port < 0xC9 && getEeprom() != nullptr )
  {
    return getEeprom()->readPort( static_cast<std::uint8_t>( port - 0xC4U ) );
  }
  if ( mRtc )
  {
    if ( auto const value = read2003( *mRtc, port ) )
    {
      return *value;
    }
  }
  return mSystem->getMemoryManager()->getUnmappedPort();
}

std::uint8_t Cart::peekPort( std::uint16_t port )
{
  if ( mRtc && port == 0xCA )
  {
    return static_cast<std::uint8_t>( ( mState.rtc.ready ? 0x80U : 0U ) | mState.rtc.command );
  }
  if ( mRtc && port == 0xCB )
  {
    return mState.rtc.data;
  }
  return readPort( port );
}

void Cart::writePort( std::uint16_t port, std::uint8_t value )
{
  if ( port < 0xC4 )
  {
    std::uint16_t& bank = mState.selectedBanks[port - 0xC0U];
    bank = mRtc ? withLow( bank, value ) : value;
    mSystem->getMemoryManager()->refreshMappings();
  }
  else if ( port < 0xC9 && getEeprom() != nullptr )
  {
    getEeprom()->writePort( static_cast<std::uint8_t>( port - 0xC4U ), value );
  }
  else if ( mRtc )
  {
    write2003( *mRtc, port, value );
  }
}

std::optional<std::uint8_t> Cart::read2003( Rtc& rtc, std::uint16_t port )
{
  auto const& banks = mState.selectedBanks;
  switch ( port )
  {
  case 0xCA:
    return rtc.readControl();
  case 0xCB:
    return rtc.readData();
  case 0xCC:
    return mState.gpoEnable;
  case 0xCD:
    return mState.gpoData;
  case 0xCE:
    return static_cast<std::uint8_t>( mState.flashSelected ? 1 : 0 );
  case 0xCF:
    return static_cast<std::uint8_t>( banks[0] );
  case 0xD0:
  case 0xD2:
  case 0xD4:
    return static_cast<std::uint8_t>( banks[( ( port - 0xD0U ) / 2 ) + 1] );
  case 0xD1:
  case 0xD3:
  case 0xD5:
    return static_cast<std::uint8_t>( banks[( ( port - 0xD1U ) / 2 ) + 1] >> 8U );
  default:
    return std::nullopt;
  }
}

void Cart::write2003( Rtc& rtc, std::uint16_t port, std::uint8_t value )
{
  auto& banks = mState.selectedBanks;
  switch ( port )
  {
  case 0xCA:
    rtc.writeControl( value );
    break;
  case 0xCB:
    rtc.writeData( value );
    break;
  case 0xCC:
    mState.gpoEnable = value & 0x0FU;
    break;
  case 0xCD:
    mState.gpoData = value & 0x0FU;
    break;
  case 0xCE:
    mState.flashSelected = ( value & 0x01U ) != 0;
    mSystem->getMemoryManager()->refreshMappings();
    break;
  case 0xCF:
    banks[0] = withLow( banks[0], value );
    mSystem->getMemoryManager()->refreshMappings();
    break;
  case 0xD0:
  case 0xD2:
  case 0xD4:
  {
    std::uint16_t& bank = banks[( ( port - 0xD0U ) / 2 ) + 1];
    bank = withLow( bank, value );
    mSystem->getMemoryManager()->refreshMappings();
    break;
  }
  case 0xD1:
  case 0xD3:
  case 0xD5:
  {
    std::uint16_t& bank = banks[( ( port - 0xD1U ) / 2 ) + 1];
    bank = static_cast<std::uint16_t>( ( bank & 0x00FFU ) | ( ( value << 8U ) & HIGH_BANK_BITS ) );
    mSystem->getMemoryManager()->refreshMappings();
    break;
  }
  default:
    break;
  }
}

void Cart::run()
{
  if ( mRtc )
  {
    mRtc->run();
  }
  if ( mFlash )
  {
    mFlash->run();
  }
}

} // namespace ws::machine
