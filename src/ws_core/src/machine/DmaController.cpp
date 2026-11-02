// The general DMA and the sound DMA of the Color. Ported from Mesen2's
// Core/WS/WsDmaController.cpp at b9fa69ddc6d0a331fb103fdb5eef6904305703c2.

#include "DmaController.hpp"

#include "Apu.hpp"
#include "MemoryManager.hpp"

namespace ws::machine
{

namespace
{

/// Mesen2's BitUtilities::SetBits: replaces the byte at BIT of `dst`.
template <unsigned BIT, class T>
void setBits( T& dst, std::uint8_t src )
{
  dst = static_cast<T>( ( static_cast<std::uint32_t>( dst ) & ~( 0xFFU << BIT ) ) |
                        ( static_cast<std::uint32_t>( src ) << BIT ) );
}

/// Mesen2's BitUtilities::GetBits: the byte at BIT of `value`.
template <unsigned BIT, class T>
std::uint8_t getBits( T value )
{
  return static_cast<std::uint8_t>( static_cast<std::uint32_t>( value ) >> BIT );
}

/// The segment and offset a 20-bit address is read through.
std::uint16_t segmentOf( std::uint32_t addr )
{
  return static_cast<std::uint16_t>( ( addr >> 4U ) & 0xF000U );
}

std::uint16_t offsetOf( std::uint32_t addr )
{
  return static_cast<std::uint16_t>( addr );
}

} // namespace

DmaController::DmaController( System& system ) : mSystem{ &system } {}

void DmaController::runGeneralDma()
{
  MemoryManager* memoryManager = mSystem->getMemoryManager();
  int const offset = ( mState.gdmaControl & 0x40U ) != 0 ? -2 : 2;

  if ( mState.gdmaLength == 0 || memoryManager->getWaitStates( mState.gdmaSrc ) > 1 ||
       !memoryManager->isWordBus( mState.gdmaSrc ) )
  {
    // When length is 0, or if transfer has an invalid source address, stop processing immediately
    // The 5 cycles of setup time aren't executed in this scenario
    return;
  }

  // GDMA takes 5+2*length cycles
  memoryManager->exec();
  memoryManager->exec();
  memoryManager->exec();
  memoryManager->exec();
  memoryManager->exec();

  while ( mState.gdmaLength != 0 )
  {
    if ( memoryManager->getWaitStates( mState.gdmaSrc ) > 1 || !memoryManager->isWordBus( mState.gdmaSrc ) )
    {
      // ROM with wait states and 8-bit buses are not allowed (SRAM, cart when 16-bit flag isn't set)
      break;
    }

    auto const value = memoryManager->read<std::uint16_t>(
        segmentOf( mState.gdmaSrc ), offsetOf( mState.gdmaSrc ), MemoryOperationType::DMA_READ );
    memoryManager->write<std::uint16_t>( 0, mState.gdmaDest, value, MemoryOperationType::DMA_WRITE );

    mState.gdmaSrc = ( mState.gdmaSrc + static_cast<std::uint32_t>( offset ) ) & 0xFFFFFU;
    mState.gdmaDest = static_cast<std::uint16_t>( mState.gdmaDest + offset );
    mState.gdmaLength = static_cast<std::uint16_t>( mState.gdmaLength - 2 );
  }
}

void DmaController::processSoundDma()
{
  if ( !mState.sdmaEnabled )
  {
    return;
  }

  if ( mState.sdmaTimer == 0 )
  {
    MemoryManager* memoryManager = mSystem->getMemoryManager();
    mState.sdmaTimer = mState.sdmaFrequency;

    // Sound DMA steals 6 cycles + N access cycles to run
    // 4 cycles at the start + N read cycles + 1 dma write cycle
    memoryManager->exec();
    memoryManager->exec();
    memoryManager->exec();
    memoryManager->exec();
    memoryManager->exec();

    auto sampleValue = memoryManager->read<std::uint8_t>(
        segmentOf( mState.sdmaSrc ), offsetOf( mState.sdmaSrc ), MemoryOperationType::DMA_READ );

    if ( mState.sdmaHold )
    {
      sampleValue = 0;
    }

    if ( !mState.sdmaHold )
    {
      std::uint32_t const offset = mState.sdmaDecrement ? 0xFFFFFFFFU : 1U;
      mState.sdmaSrc = ( mState.sdmaSrc + offset ) & 0xFFFFFU;
      mState.sdmaLength--;

      if ( mState.sdmaLength == 0 )
      {
        if ( mState.sdmaRepeat )
        {
          mState.sdmaSrc = mState.sdmaSrcReloadValue;
          mState.sdmaLength = mState.sdmaLengthReloadValue;
        }
        else
        {
          mState.sdmaEnabled = false;
          mState.sdmaControl &= 0x7FU;
        }
      }
    }
    else
    {
      sampleValue = 0;
    }

    memoryManager->exec();
    mSystem->getApu()->writeDma( mState.sdmaHyperVoice, sampleValue );
  }
  else
  {
    mState.sdmaTimer--;
  }
}

std::uint8_t DmaController::readPort( std::uint16_t port )
{
  switch ( port )
  {
  case 0x40:
    return getBits<0>( mState.gdmaSrc );
  case 0x41:
    return getBits<8>( mState.gdmaSrc );
  case 0x42:
    return getBits<16>( mState.gdmaSrc );
  case 0x43:
    return 0;
  case 0x44:
    return getBits<0>( mState.gdmaDest );
  case 0x45:
    return getBits<8>( mState.gdmaDest );
  case 0x46:
    return getBits<0>( mState.gdmaLength );
  case 0x47:
    return getBits<8>( mState.gdmaLength );
  case 0x48:
    return mState.gdmaControl;

  case 0x4A:
    return getBits<0>( mState.sdmaSrc );
  case 0x4B:
    return getBits<8>( mState.sdmaSrc );
  case 0x4C:
    return getBits<16>( mState.sdmaSrc );
  case 0x4D:
    return 0;
  case 0x4E:
    return getBits<0>( mState.sdmaLength );
  case 0x4F:
    return getBits<8>( mState.sdmaLength );
  case 0x50:
    return getBits<16>( mState.sdmaLength );
  case 0x51:
    return 0;

  case 0x52:
    return mState.sdmaControl;
  case 0x53:
    return 0;

  default:
    break;
  }

  return mSystem->getMemoryManager()->getUnmappedPort();
}

void DmaController::writePort( std::uint16_t port, std::uint8_t value )
{
  switch ( port )
  {
  case 0x40:
    setBits<0>( mState.gdmaSrc, value & 0xFEU );
    break;
  case 0x41:
    setBits<8>( mState.gdmaSrc, value );
    break;
  case 0x42:
    setBits<16>( mState.gdmaSrc, value & 0x0FU );
    break;
  case 0x43:
    break;
  case 0x44:
    setBits<0>( mState.gdmaDest, value & 0xFEU );
    break;
  case 0x45:
    setBits<8>( mState.gdmaDest, value );
    break;
  case 0x46:
    setBits<0>( mState.gdmaLength, value & 0xFEU );
    break;
  case 0x47:
    setBits<8>( mState.gdmaLength, value );
    break;
  case 0x48:
    mState.gdmaControl = value & 0x40U;
    if ( ( value & 0x80U ) != 0 )
    {
      runGeneralDma();
    }
    break;

  case 0x4A:
    setBits<0>( mState.sdmaSrc, value );
    setBits<0>( mState.sdmaSrcReloadValue, value );
    break;

  case 0x4B:
    setBits<8>( mState.sdmaSrc, value );
    setBits<8>( mState.sdmaSrcReloadValue, value );
    break;

  case 0x4C:
    setBits<16>( mState.sdmaSrc, value & 0x0FU );
    setBits<16>( mState.sdmaSrcReloadValue, value & 0x0FU );
    break;

  case 0x4E:
    setBits<0>( mState.sdmaLength, value );
    setBits<0>( mState.sdmaLengthReloadValue, value );
    break;

  case 0x4F:
    setBits<8>( mState.sdmaLength, value );
    setBits<8>( mState.sdmaLengthReloadValue, value );
    break;

  case 0x50:
    setBits<16>( mState.sdmaLength, value & 0x0FU );
    setBits<16>( mState.sdmaLengthReloadValue, value & 0x0FU );
    break;

  case 0x52:
    if ( mState.sdmaLength == 0 )
    {
      // Clear enable flag if length is 0 (based on sound_dma + WSHSTest results)
      value &= 0x7FU;
    }

    mState.sdmaControl = value & 0xDFU;

    switch ( value & 0x03U )
    {
    default:
    case 0:
      mState.sdmaFrequency = 5;
      break;
    case 1:
      mState.sdmaFrequency = 3;
      break;
    case 2:
      mState.sdmaFrequency = 1;
      break;
    case 3:
      mState.sdmaFrequency = 0;
      break;
    }

    mState.sdmaHold = ( value & 0x04U ) != 0;
    mState.sdmaRepeat = ( value & 0x08U ) != 0;
    mState.sdmaHyperVoice = ( value & 0x10U ) != 0;
    mState.sdmaDecrement = ( value & 0x40U ) != 0;
    mState.sdmaEnabled = ( value & 0x80U ) != 0;
    break;

  default:
    break;
  }
}

} // namespace ws::machine
