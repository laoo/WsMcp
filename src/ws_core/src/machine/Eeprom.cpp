#include "Eeprom.hpp"

#include <algorithm>
#include <string_view>

namespace ws::machine
{

Eeprom::Eeprom( System& system, std::span<std::uint8_t> data, bool isInternal, bool bootRom )
    : mSystem{ &system }, mData{ data }, mIsInternal{ isInternal }
{
  mState.size = static_cast<EepromSize>( data.size() );
  mState.idle = true;
  mState.readDone = true;
  mState.writeDisabled = true;

  if ( mIsInternal && !bootRom )
  {
    // Boot ROM leaves writes enabled
    mState.writeDisabled = false;
  }
}

EepromCommand Eeprom::getCommand() const
{
  unsigned command = mState.command;
  if ( getSize() != EepromSize::SIZE_128 )
  {
    command >>= 4U;
  }

  switch ( command & 0xFFC0U )
  {
  case 0x0180:
    return EepromCommand::READ;
  case 0x0140:
    return EepromCommand::WRITE;
  case 0x01C0:
    return EepromCommand::ERASE;
  case 0x0100:
    switch ( ( command >> 4U ) & 0x03U )
    {
    case 0x00:
      return EepromCommand::WRITE_DISABLE;
    case 0x01:
      return EepromCommand::WRITE_ALL;
    case 0x02:
      return EepromCommand::ERASE_ALL;
    default:
      return EepromCommand::WRITE_ENABLE;
    }
  default:
    return EepromCommand::UNKNOWN;
  }
}

EepromSize Eeprom::getSize() const
{
  if ( mIsInternal && mState.size == EepromSize::SIZE_2KB && !mSystem->isColorMode() )
  {
    // Process commands as is the eeprom was the monochrome model's 128-byte eeprom when color mode is disabled
    return EepromSize::SIZE_128;
  }
  return mState.size;
}

std::uint16_t Eeprom::getCommandAddress() const
{
  switch ( getSize() )
  {
  case EepromSize::SIZE_128:
    return mState.command & 0x3FU;
  case EepromSize::SIZE_1KB:
    return mState.command & 0x1FFU;
  case EepromSize::SIZE_2KB:
    return mState.command & 0x3FFU;
  default:
    return 0;
  }
}

void Eeprom::writeValue( std::uint16_t addr, std::uint16_t value )
{
  if ( !mState.writeDisabled && ( !mState.internalEepromWriteProtected || addr < 0x30 ) )
  {
    std::size_t const at = static_cast<std::size_t>( addr ) << 1U;
    if ( at + 1 < mData.size() )
    {
      mData[at] = static_cast<std::uint8_t>( value );
      mData[at + 1] = static_cast<std::uint8_t>( value >> 8U );
    }
  }
}

void Eeprom::initInternalEepromData( Model model, std::span<std::uint8_t> data )
{
  std::ranges::fill( data, std::uint8_t{} );

  std::string_view name;
  switch ( model )
  {
  case Model::MONOCHROME:
    name = "WONDERSWAN";
    break;

  case Model::COLOR:
  case Model::SWAN_CRYSTAL:
    // Set volume to max by default
    data[0x83] = 0x03;
    name = model == Model::COLOR ? "WONDERSWANCOLOR" : "SWANCRYSTAL";
    break;
  }

  // The IPL's own character set: '0' is 1, 'A' is 11.
  for ( std::size_t i = 0; i < name.size(); i++ )
  {
    data[0x60 + i] = static_cast<std::uint8_t>( name[i] - 0x36 );
  }
}

void Eeprom::run()
{
  if ( mState.cmdStartClock == 0 )
  {
    return;
  }

  EepromCommand const cmd = getCommand();
  std::uint64_t const cmdDelay = 10; // TODOWS timing
  if ( mSystem->getMasterClock() - mState.cmdStartClock > cmdDelay )
  {
    std::uint16_t const addr = getCommandAddress();
    std::size_t const at = static_cast<std::size_t>( addr ) << 1U;
    auto const size = static_cast<unsigned>( mState.size );

    switch ( cmd )
    {
    case EepromCommand::READ:
      if ( at + 1 < mData.size() )
      {
        mState.readBuffer = static_cast<std::uint16_t>( mData[at] | ( mData[at + 1] << 8U ) );
      }
      mState.readDone = true;
      break;

    case EepromCommand::WRITE:
      writeValue( addr, mState.writeBuffer );
      break;

    case EepromCommand::WRITE_ALL:
      for ( unsigned i = 0; i < size; i += 2 )
      {
        writeValue( static_cast<std::uint16_t>( i >> 1U ), mState.writeBuffer );
      }
      break;

    case EepromCommand::ERASE:
      writeValue( addr, 0xFFFF );
      break;

    case EepromCommand::ERASE_ALL:
      for ( unsigned i = 0; i < size; i += 2 )
      {
        writeValue( static_cast<std::uint16_t>( i >> 1U ), 0xFFFF );
      }
      break;

    default:
      break;
    }

    mState.idle = true;
    mState.cmdStartClock = 0;
  }
}

void Eeprom::writePort( std::uint8_t port, std::uint8_t value )
{
  run();

  switch ( port )
  {
  case 0x00:
    mState.writeBuffer = static_cast<std::uint16_t>( ( mState.writeBuffer & 0xFF00U ) | value );
    break;
  case 0x01:
    mState.writeBuffer =
        static_cast<std::uint16_t>( ( mState.writeBuffer & 0x00FFU ) | ( static_cast<unsigned>( value ) << 8U ) );
    break;
  case 0x02:
    mState.command = static_cast<std::uint16_t>( ( mState.command & 0xFF00U ) | value );
    break;
  case 0x03:
    mState.command =
        static_cast<std::uint16_t>( ( mState.command & 0x00FFU ) | ( static_cast<unsigned>( value ) << 8U ) );
    break;

  case 0x04:
  {
    bool const read = ( value & 0x10U ) != 0;
    bool const write = ( value & 0x20U ) != 0;
    bool const other = ( value & 0x40U ) != 0;
    bool const writeProtect = ( value & 0x80U ) != 0;
    if ( static_cast<int>( read ) + static_cast<int>( write ) + static_cast<int>( other ) +
             static_cast<int>( writeProtect ) >
         1 )
    {
      // Invalid operation, do nothing
      return;
    }

    // TODOWS abort (cart eeprom)
    if ( writeProtect && mIsInternal )
    {
      mState.internalEepromWriteProtected = true;
      return;
    }

    if ( !mState.idle )
    {
      return;
    }

    EepromCommand const cmd = getCommand();

    if ( read )
    {
      if ( cmd == EepromCommand::READ )
      {
        mState.cmdStartClock = mSystem->getMasterClock();

        if ( mIsInternal )
        {
          // For the internal eeprom, "ReadDone" flag should be cleared here, and set again after a small delay
          // For the cartridge eeprom, the flag is bugged and not reset when a read starts
          mState.readDone = false;
        }
        mState.idle = false;
      }
    }
    else if ( write )
    {
      if ( cmd == EepromCommand::WRITE || cmd == EepromCommand::WRITE_ALL )
      {
        mState.readDone = false;
        mState.cmdStartClock = mSystem->getMasterClock();
        mState.idle = false;
      }
    }
    else if ( other )
    {
      mState.readDone = false;

      switch ( cmd )
      {
      case EepromCommand::ERASE:
      case EepromCommand::ERASE_ALL:
        mState.cmdStartClock = mSystem->getMasterClock();
        break;

      case EepromCommand::WRITE_DISABLE:
        mState.writeDisabled = true;
        break;

      case EepromCommand::WRITE_ENABLE:
        mState.writeDisabled = false;
        break;

      default:
        break;
      }
    }
    break;
  }

  default:
    break;
  }
}

std::uint8_t Eeprom::readPort( std::uint8_t port )
{
  run();

  switch ( port )
  {
  case 0x00:
    return static_cast<std::uint8_t>( mState.readBuffer );
  case 0x01:
    return static_cast<std::uint8_t>( mState.readBuffer >> 8U );
  case 0x02:
    return static_cast<std::uint8_t>( mState.command );
  case 0x03:
    return static_cast<std::uint8_t>( mState.command >> 8U );
  case 0x04:
    return static_cast<std::uint8_t>( ( mState.readDone ? 0x01U : 0U ) | ( mState.idle ? 0x02U : 0U ) |
                                      ( mState.internalEepromWriteProtected ? 0x80U : 0U ) );
  default:
    return 0;
  }
}

} // namespace ws::machine
