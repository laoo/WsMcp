#include "Cart.hpp"

#include "Eeprom.hpp"
#include "MemoryManager.hpp"

namespace ws::machine
{

Cart::Cart( System& system ) : mSystem{ &system }
{
  mState.selectedBanks = { 0xFF, 0xFF, 0xFF, 0xFF };
}

Eeprom* Cart::getEeprom() const
{
  return mSystem->getCartEeprom();
}

void Cart::refreshMappings()
{
  MemoryManager* const memoryManager = mSystem->getMemoryManager();
  memoryManager->map( 0x10000, 0x1FFFF, MemoryType::CART_RAM, mState.selectedBanks[1] * 0x10000U, false );
  memoryManager->map( 0x20000, 0x2FFFF, MemoryType::PRG_ROM, mState.selectedBanks[2] * 0x10000U, true );
  memoryManager->map( 0x30000, 0x3FFFF, MemoryType::PRG_ROM, mState.selectedBanks[3] * 0x10000U, true );
  memoryManager->map( 0x40000, 0xFFFFF, MemoryType::PRG_ROM, ( mState.selectedBanks[0] * 0x100000U ) + 0x40000U, true );
}

std::uint8_t Cart::readPort( std::uint16_t port )
{
  if ( port < 0xC4 )
  {
    return mState.selectedBanks[port - 0xC0U];
  }
  if ( port < 0xC9 && getEeprom() != nullptr )
  {
    return getEeprom()->readPort( static_cast<std::uint8_t>( port - 0xC4U ) );
  }
  return mSystem->getMemoryManager()->getUnmappedPort();
}

void Cart::writePort( std::uint16_t port, std::uint8_t value )
{
  if ( port < 0xC4 )
  {
    mState.selectedBanks[port - 0xC0U] = value;
    mSystem->getMemoryManager()->refreshMappings();
  }
  else if ( port < 0xC9 && getEeprom() != nullptr )
  {
    getEeprom()->writePort( static_cast<std::uint8_t>( port - 0xC4U ), value );
  }
}

} // namespace ws::machine
