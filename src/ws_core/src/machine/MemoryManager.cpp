#include "MemoryManager.hpp"

#include "Cart.hpp"
#include "ControlManager.hpp"
#include "DmaController.hpp"
#include "Eeprom.hpp"
#include "Serial.hpp"
#include "Timer.hpp"

namespace ws::machine
{

MemoryManager::MemoryManager( System& system ) : mSystem{ &system } {}

void MemoryManager::init()
{
  mCpu = mSystem->getCpu();
  mPpu = mSystem->getPpu();
  mApu = mSystem->getApu();
  refreshMappings();
}

void MemoryManager::refreshMappings()
{
  mSystem->getCart()->refreshMappings();
  if ( mState.colorEnabled )
  {
    map( 0, 0xFFFF, MemoryType::WORK_RAM, 0, false );
  }
  else
  {
    unmap( 0x4000, 0xFFFF );
    map( 0, 0x3FFF, MemoryType::WORK_RAM, 0, false );
  }

  if ( !mState.bootRomDisabled )
  {
    auto const bootRomSize = static_cast<std::uint32_t>( mSystem->getMemory( MemoryType::BOOT_ROM ).size() );
    map( 0x100000 - bootRomSize, 0xFFFFF, MemoryType::BOOT_ROM, 0, true );
  }
}

void MemoryManager::map( std::uint32_t start, std::uint32_t end, MemoryType type, std::uint32_t offset, bool readonly )
{
  std::span<std::uint8_t> const memory = mSystem->getMemory( type );
  auto const size = static_cast<std::uint32_t>( memory.size() );

  if ( size == 0 )
  {
    unmap( start, end );
    return;
  }

  offset %= size;
  for ( std::uint32_t i = start; i < end; i += 0x1000 )
  {
    std::uint8_t* const src = memory.data() + offset;
    mReads[i >> 12U] = src;
    mWrites[i >> 12U] = readonly ? nullptr : src;

    offset += 0x1000;
    if ( offset >= size )
    {
      offset = 0;
    }
  }
}

void MemoryManager::unmap( std::uint32_t start, std::uint32_t end )
{
  for ( std::uint32_t i = start; i < end; i += 0x1000 )
  {
    mReads[i >> 12U] = nullptr;
    mWrites[i >> 12U] = nullptr;
  }
}

std::uint8_t MemoryManager::debugRead( std::uint32_t addr ) const
{
  std::uint8_t const* handler = mReads[( addr >> 12U ) & 0xFFU];
  if ( handler != nullptr )
  {
    return handler[addr & 0xFFFU];
  }
  return 0;
}

void MemoryManager::debugWrite( std::uint32_t addr, std::uint8_t value )
{
  std::uint8_t* handler = mWrites[( addr >> 12U ) & 0xFFU];
  if ( handler != nullptr )
  {
    handler[addr & 0xFFFU] = value;
  }
}

template <typename T>
T MemoryManager::debugCpuRead( std::uint16_t seg, std::uint16_t offset ) const
{
  std::uint32_t const addr = ( ( static_cast<std::uint32_t>( seg ) << 4U ) + offset ) & 0xFFFFFU;
  if constexpr ( std::is_same_v<T, std::uint16_t> )
  {
    bool const splitReads = !isWordBus( addr ) || ( addr & 0x01U ) != 0;
    if ( splitReads )
    {
      auto const lo = debugCpuRead<std::uint8_t>( seg, offset );
      auto const hi = debugCpuRead<std::uint8_t>( seg, static_cast<std::uint16_t>( offset + 1 ) );
      return static_cast<std::uint16_t>( lo | ( hi << 8U ) );
    }
    std::uint8_t const lo = debugRead( addr );
    std::uint8_t const hi = debugRead(
        ( ( static_cast<std::uint32_t>( seg ) << 4U ) + static_cast<std::uint16_t>( offset + 1 ) ) & 0xFFFFFU );
    return static_cast<std::uint16_t>( lo | ( hi << 8U ) );
  }
  else
  {
    return debugRead( addr );
  }
}

template <typename T>
T MemoryManager::readPort( std::uint16_t port )
{
  if constexpr ( std::is_same_v<T, std::uint16_t> )
  {
    bool const splitReads = !isWordPort( port ) || ( port & 0x01U ) != 0;
    if ( splitReads )
    {
      auto const lo = readPort<std::uint8_t>( port );
      auto const hi = readPort<std::uint8_t>( static_cast<std::uint16_t>( port + 1 ) );
      return static_cast<std::uint16_t>( lo | ( hi << 8U ) );
    }
    exec();

    std::uint16_t value = 0;
    if ( isUnmappedPort( port ) )
    {
      value = static_cast<std::uint16_t>( getUnmappedPort() | ( getUnmappedPort() << 8U ) );
    }
    else
    {
      std::uint8_t const lo = internalReadPort( port, true );
      std::uint8_t const hi = internalReadPort( static_cast<std::uint16_t>( port + 1 ), true );
      value = static_cast<std::uint16_t>( lo | ( hi << 8U ) );
    }

    mSystem->processPortAccess( port, value, true, false );
    return value;
  }
  else
  {
    exec();

    std::uint8_t const value = isUnmappedPort( port ) ? getUnmappedPort() : internalReadPort( port, false );
    mSystem->processPortAccess( port, value, false, false );
    return value;
  }
}

template <typename T>
void MemoryManager::writePort( std::uint16_t port, T value )
{
  if constexpr ( std::is_same_v<T, std::uint16_t> )
  {
    bool const splitWrites = !isWordPort( port ) || ( port & 0x01U ) != 0;
    if ( splitWrites )
    {
      writePort<std::uint8_t>( port, static_cast<std::uint8_t>( value ) );
      writePort<std::uint8_t>( static_cast<std::uint16_t>( port + 1 ), static_cast<std::uint8_t>( value >> 8U ) );
    }
    else
    {
      exec();
      mSystem->processPortAccess( port, value, true, true );

      if ( !isUnmappedPort( port ) )
      {
        internalWritePort( port, static_cast<std::uint8_t>( value ), true );
        internalWritePort( static_cast<std::uint16_t>( port + 1 ), static_cast<std::uint8_t>( value >> 8U ), true );
      }
    }
  }
  else
  {
    exec();
    mSystem->processPortAccess( port, value, false, true );

    if ( !isUnmappedPort( port ) )
    {
      internalWritePort( port, value, false );
    }
  }
}

template <typename T>
T MemoryManager::debugReadPort( std::uint16_t port )
{
  if constexpr ( std::is_same_v<T, std::uint16_t> )
  {
    auto const lo = debugReadPort<std::uint8_t>( port );
    auto const hi = debugReadPort<std::uint8_t>( static_cast<std::uint16_t>( port + 1 ) );
    return static_cast<std::uint16_t>( lo | ( hi << 8U ) );
  }
  else
  {
    if ( isUnmappedPort( port ) )
    {
      return getUnmappedPort();
    }

    if ( port == 0xB1 || port == 0xB3 || port == 0xB5 )
    {
      // TODOWS implement peek to avoid side-effects for these ports
      return 0;
    }
    if ( port == 0xCA || port == 0xCB )
    {
      // The 2003's RTC moves a command on when $CB is read.
      return mSystem->getCart()->peekPort( port );
    }

    return internalReadPort( port, false );
  }
}

bool MemoryManager::isUnmappedPort( std::uint16_t port )
{
  return ( port & 0x100U ) != 0 || ( port >= 0x100 && ( port & 0xFFU ) >= 0xB7 );
}

std::uint8_t MemoryManager::getUnmappedPort() const
{
  return mSystem->getModel() == Model::MONOCHROME ? 0x90 : 0;
}

std::uint8_t MemoryManager::internalReadPort( std::uint16_t port, bool isWordAccess )
{
  port &= 0xFFU;
  Model const model = mSystem->getModel();

  if ( port <= 0x3F )
  {
    return mPpu->readPort( port );
  }
  if ( port >= 0x40 && port <= 0x53 && model != Model::MONOCHROME )
  {
    return mState.colorEnabled ? mSystem->getDmaController()->readPort( port ) : 0;
  }
  if ( port >= 0x6A && port <= 0x6B && model != Model::MONOCHROME )
  {
    return mApu->read( port ); // HyperVoice
  }
  if ( port >= 0x70 && port <= 0x77 && model == Model::SWAN_CRYSTAL )
  {
    return mPpu->readLcdConfigPort( port );
  }
  if ( port >= 0x80 && port <= 0x9F )
  {
    return mApu->read( port );
  }
  if ( port >= 0xA2 && port <= 0xAB && port != 0xA3 )
  {
    return mSystem->getTimer()->readPort( port );
  }
  if ( port >= 0xBA && port <= 0xBF )
  {
    if ( model != Model::MONOCHROME || isWordAccess || ( port & 0x01U ) == 0 )
    {
      return mSystem->getInternalEeprom()->readPort( static_cast<std::uint8_t>( port - 0xBA ) );
    }
    return getUnmappedPort();
  }
  if ( port >= 0xC0 )
  {
    return mSystem->getCart()->readPort( port );
  }

  switch ( port )
  {
  case 0x60:
    return model == Model::MONOCHROME ? getUnmappedPort() : mState.systemControl2;

  case 0x62:
    if ( model == Model::MONOCHROME )
    {
      return getUnmappedPort();
    }
    return static_cast<std::uint8_t>( ( mState.powerOffRequested ? 0x01U : 0U ) |
                                      ( model == Model::SWAN_CRYSTAL ? 0x80U : 0U ) );

  case 0xA0:
    return static_cast<std::uint8_t>(
        ( mState.bootRomDisabled ? 0x01U : 0U ) | ( model == Model::MONOCHROME ? 0U : 0x02U ) |
        ( mState.cartWordBus ? 0x04U : 0U ) | ( mState.slowRom ? 0x08U : 0U ) | 0x80U // mbc authentication?
    );

  case 0xA3:
    return mState.systemTest;
  case 0xB0:
    return getIrqVector();
  case 0xB1:
    return mSystem->getSerial()->read( port );
  case 0xB2:
    return mState.enabledIrqs;
  case 0xB3:
    return mSystem->getSerial()->read( port );
  case 0xB4:
    return getActiveIrqs();
  case 0xB5:
    return mSystem->getControlManager()->read();
  case 0xB6:
    return 0;
  case 0xB7:
    return mState.enableLowBatteryNmi ? 0x10 : 0;
  default:
    return getUnmappedPort();
  }
}

void MemoryManager::internalWritePort( std::uint16_t port, std::uint8_t value, bool isWordAccess )
{
  port &= 0xFFU;
  Model const model = mSystem->getModel();

  // The HyperVoice's ports reach the APU as its others do; the Color alone decodes them.
  // NOLINTBEGIN(bugprone-branch-clone)
  if ( port <= 0x3F )
  {
    mPpu->writePort( port, value );
  }
  else if ( port >= 0x40 && port <= 0x53 && mState.colorEnabled )
  {
    mSystem->getDmaController()->writePort( port, value );
  }
  else if ( port >= 0x64 && port <= 0x6B && model != Model::MONOCHROME )
  {
    mApu->write( port, value ); // HyperVoice
  }
  else if ( port >= 0x70 && port <= 0x77 && !mState.bootRomDisabled && model == Model::SWAN_CRYSTAL )
  {
    mPpu->writeLcdConfigPort( port, value );
  }
  else if ( port >= 0x80 && port <= 0x9F )
  {
    mApu->write( port, value );
  }
  else if ( port >= 0xA2 && port <= 0xAB && port != 0xA3 )
  {
    mSystem->getTimer()->writePort( port, value );
  }
  else if ( port >= 0xBA && port <= 0xBF )
  {
    if ( model != Model::MONOCHROME || isWordAccess || ( port & 0x01U ) == 0 )
    {
      mSystem->getInternalEeprom()->writePort( static_cast<std::uint8_t>( port - 0xBA ), value );
    }
  }
  else if ( port >= 0xC0 )
  {
    mSystem->getCart()->writePort( port, value );
  }
  else
  {
    switch ( port )
    {
    case 0x60:
      if ( model != Model::MONOCHROME )
      {
        mState.systemControl2 = value & 0xEFU;
        mState.colorEnabled = ( value & 0x80U ) != 0;
        mState.enable4bpp = ( value & 0x40U ) != 0;
        mState.enable4bppPacked = ( value & 0x20U ) != 0;
        mState.slowSram = ( value & 0x02U ) != 0;
        mState.slowPort = ( value & 0x08U ) != 0;
        refreshMappings();
        VideoMode mode = VideoMode::MONOCHROME;
        if ( mState.colorEnabled )
        {
          mode = VideoMode::COLOR_2BPP;
          if ( mState.enable4bpp )
          {
            mode = mState.enable4bppPacked ? VideoMode::COLOR_4BPP_PACKED : VideoMode::COLOR_4BPP;
          }
        }
        mPpu->setVideoMode( mode );
      }
      break;

    case 0x62:
      if ( model != Model::MONOCHROME )
      {
        mState.powerOffRequested = ( value & 0x01U ) != 0;
      }
      break;

    case 0xA0:
      mState.bootRomDisabled = ( value & 0x01U ) != 0 || mState.bootRomDisabled;
      mState.cartWordBus = ( value & 0x04U ) != 0;
      mState.slowRom = ( value & 0x08U ) != 0;
      refreshMappings();
      break;

    case 0xA3:
      // TODOWS SystemTest is not implemented
      if ( !isWordAccess )
      {
        mState.systemTest = value & 0x0FU;
      }
      break;

    case 0xB0:
      mState.irqVectorOffset = value & 0xF8U;
      break;
    case 0xB1:
      mSystem->getSerial()->write( port, value );
      break;
    case 0xB2:
      mState.enabledIrqs = value;
      break;
    case 0xB3:
      mSystem->getSerial()->write( port, value );
      break;
    case 0xB5:
      mSystem->getControlManager()->write( value );
      break;
    case 0xB6:
      mState.activeIrqs = static_cast<std::uint8_t>( mState.activeIrqs & ~value );
      break;
    case 0xB7:
      mState.enableLowBatteryNmi = ( value & 0x10U ) != 0;
      break;

    default:
      break;
    }
  }
  // NOLINTEND(bugprone-branch-clone)
}

bool MemoryManager::isWordBus( std::uint32_t addr ) const
{
  switch ( addr >> 16U )
  {
  case 0:
    return true;
  case 1:
    return false;
  default:
    return mState.cartWordBus;
  }
}

std::uint8_t MemoryManager::getWaitStates( std::uint32_t addr ) const
{
  switch ( addr >> 16U )
  {
  case 0:
    return 1;
  case 1:
    return static_cast<std::uint8_t>( 1 + static_cast<int>( mState.slowSram ) );
  default:
    return static_cast<std::uint8_t>( 1 + static_cast<int>( mState.slowRom ) );
  }
}

bool MemoryManager::isWordPort( std::uint16_t port )
{
  return port < 0xC0;
}

std::uint8_t MemoryManager::getPortWaitStates( std::uint16_t port ) const
{
  return static_cast<std::uint8_t>( 1 + static_cast<int>( mState.slowPort && port >= 0xC0 && port <= 0xFF ) );
}

void MemoryManager::setIrqSource( IrqSource src )
{
  auto const bit = static_cast<std::uint8_t>( src );
  if ( ( mState.enabledIrqs & bit ) != 0 )
  {
    mState.activeIrqs |= bit;
  }
}

void MemoryManager::clearIrqSource( IrqSource src )
{
  // Mesen2 keeps only the source given here, rather than clearing it; ported
  // as it is, as nothing calls it there.
  mState.activeIrqs &= static_cast<std::uint8_t>( src );
}

std::uint8_t MemoryManager::getActiveIrqs()
{
  if ( mSystem->getSerial()->hasSendIrq() &&
       ( mState.enabledIrqs & static_cast<std::uint8_t>( IrqSource::UART_SEND_READY ) ) != 0 )
  {
    mState.activeIrqs |= static_cast<std::uint8_t>( IrqSource::UART_SEND_READY );
  }
  return mState.activeIrqs;
}

std::uint8_t MemoryManager::getIrqVector()
{
  std::uint8_t const activeIrqs = getActiveIrqs();
  for ( int i = 7; i >= 0; i-- )
  {
    if ( ( activeIrqs & ( 1U << static_cast<unsigned>( i ) ) ) != 0 )
    {
      return static_cast<std::uint8_t>( mState.irqVectorOffset + i );
    }
  }
  return mState.irqVectorOffset;
}

void MemoryManager::onBeforeBreak()
{
  mSystem->getInternalEeprom()->run();
  if ( Eeprom* const cartEeprom = mSystem->getCartEeprom() )
  {
    cartEeprom->run();
  }
}

template std::uint8_t MemoryManager::readPort( std::uint16_t port );
template std::uint16_t MemoryManager::readPort( std::uint16_t port );
template std::uint8_t MemoryManager::debugReadPort( std::uint16_t port );
template std::uint16_t MemoryManager::debugReadPort( std::uint16_t port );

template void MemoryManager::writePort( std::uint16_t port, std::uint8_t value );
template void MemoryManager::writePort( std::uint16_t port, std::uint16_t value );

template std::uint8_t MemoryManager::debugCpuRead( std::uint16_t seg, std::uint16_t offset ) const;
template std::uint16_t MemoryManager::debugCpuRead( std::uint16_t seg, std::uint16_t offset ) const;

} // namespace ws::machine
