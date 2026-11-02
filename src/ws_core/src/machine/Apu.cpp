// The sound: four wavetable channels, the PCM voice and the HyperVoice.
// Ported from Mesen2's Core/WS/APU/WsApu.cpp at
// b9fa69ddc6d0a331fb103fdb5eef6904305703c2.

#include "Apu.hpp"

#include "ApuChannels.hpp"
#include "DmaController.hpp"
#include "MemoryManager.hpp"
#include "Ppu.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <span>

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

std::uint8_t defaultMasterVolume( Model model )
{
  return model == Model::MONOCHROME ? 2 : 3;
}

} // namespace

Apu::Apu( System& system ) : mSystem{ &system }
{
  mState.masterVolume = defaultMasterVolume( mSystem->getModel() );
  mState.internalMasterVolume = mState.masterVolume;

  mCh1 = std::make_unique<ApuCh1>( this, mState.ch1 );
  mCh2 = std::make_unique<ApuCh2>( this, mState.ch2 );
  mCh3 = std::make_unique<ApuCh3>( this, mState.ch3 );
  mCh4 = std::make_unique<ApuCh4>( this, mState.ch4 );
  mHyperVoice = std::make_unique<HyperVoice>( mState.voice );

  mFilterL.setCutoffFrequency( 16, SAMPLE_RATE );
  mFilterR.setCutoffFrequency( 16, SAMPLE_RATE );
}

Apu::~Apu() = default;

ApuState& Apu::getState()
{
  return mState;
}

ApuState const& Apu::getState() const
{
  return mState;
}

std::uint8_t Apu::getMasterVolume() const
{
  return mState.internalMasterVolume;
}

void Apu::changeMasterVolume()
{
  if ( mSystem->getConfig().audioMode == AudioMode::SPEAKERS )
  {
    if ( mState.internalMasterVolume == 0 )
    {
      mState.internalMasterVolume = defaultMasterVolume( mSystem->getModel() );
    }
    else
    {
      mState.internalMasterVolume--;
    }
  }
  mSystem->getPpu()->showVolumeIcon();
}

void Apu::playQueuedAudio()
{
  mSystem->audioDone(
      std::span<std::int16_t const>{ mSoundBuffer.data(), static_cast<std::size_t>( mSampleCount ) * 2 } );
  mSampleCount = 0;
  mClockCounter = 0;
}

void Apu::writeDma( bool forHyperVoice, std::uint8_t sampleValue )
{
  if ( forHyperVoice )
  {
    mHyperVoice->writeDma( sampleValue );
  }
  else
  {
    mState.ch2.rightVolume = sampleValue & 0x0FU;
    mState.ch2.leftVolume = static_cast<std::uint8_t>( sampleValue >> 4U );
  }
}

std::uint8_t Apu::readSample( std::uint8_t ch, std::uint8_t pos )
{
  // TODOWS review once the exact behavior for these 2 flags is understood
  if ( mState.forceOutput4 )
  {
    return 4;
  }
  if ( mState.forceOutput2 )
  {
    return 2;
  }

  std::uint32_t const addr = mState.waveTableAddress + ( ch * 16U ) + ( pos >> 1U );
  std::uint8_t const value = mSystem->getMemoryManager()->internalRead( addr );
  // The wavetable is read from work RAM: the debugger sees it as a read.
  mSystem->processMemoryAccess( addr, value, false, MemoryOperationType::READ );
  if ( ( pos & 0x01U ) != 0 )
  {
    return static_cast<std::uint8_t>( value >> 4U );
  }
  return value & 0x0FU;
}

void Apu::updateOutput()
{
  // Mesen2 scales each channel by its user-set volume here; every channel is
  // at full volume.
  std::int32_t leftOutput =
      mState.ch1.leftOutput + mState.ch2.leftOutput + mState.ch3.leftOutput + mState.ch4.leftOutput;

  std::int32_t rightOutput =
      mState.ch1.rightOutput + mState.ch2.rightOutput + mState.ch3.rightOutput + mState.ch4.rightOutput;

  if ( mState.forceOutputCh2Voice )
  {
    leftOutput = ( mState.ch2.getVolume() * 5 ) & 0x3FF;
    rightOutput = leftOutput;
  }

  if ( mSystem->getConfig().audioMode == AudioMode::HEADPHONES )
  {
    if ( mState.headphoneEnabled )
    {
      leftOutput <<= 5;
      rightOutput <<= 5;

      if ( mState.voice.enabled )
      {
        leftOutput += mState.voice.leftOutput;
        rightOutput += mState.voice.rightOutput;
      }
    }
    else
    {
      leftOutput = 0;
      rightOutput = 0;
    }
  }
  else
  {
    if ( mState.speakerEnabled )
    {
      std::int32_t const out = ( ( ( leftOutput + rightOutput ) >> mState.speakerVolume ) & 0xFF ) << 7;
      leftOutput = out;
      rightOutput = out;

      if ( mSystem->getModel() == Model::MONOCHROME )
      {
        switch ( mState.internalMasterVolume )
        {
        case 0:
          leftOutput = 0;
          rightOutput = 0;
          break;
        case 1:
          leftOutput >>= 1;
          rightOutput >>= 1;
          break;
        case 2:
        default:
          break;
        }
      }
      else
      {
        switch ( mState.internalMasterVolume )
        {
        case 0:
          leftOutput = 0;
          rightOutput = 0;
          break;
        case 1:
          leftOutput /= 3;
          rightOutput /= 3;
          break;
        case 2:
          leftOutput = leftOutput * 2 / 3;
          rightOutput = rightOutput * 2 / 3;
          break;
        case 3:
        default:
          break;
        }
      }
    }
    else
    {
      leftOutput = 0;
      rightOutput = 0;
    }
  }

  leftOutput = std::clamp<std::int32_t>(
      leftOutput, std::numeric_limits<std::int16_t>::min(), std::numeric_limits<std::int16_t>::max() );
  rightOutput = std::clamp<std::int32_t>(
      rightOutput, std::numeric_limits<std::int16_t>::min(), std::numeric_limits<std::int16_t>::max() );

  // Use low pass filter and subtract the result to filter out DC offset.
  // Mesen2 stores the difference into an int16_t as it comes: truncated
  // toward zero, and wrapped where it leaves the 16-bit range.
  double const left = leftOutput - mFilterL.process( leftOutput );
  double const right = rightOutput - mFilterR.process( rightOutput );
  auto const index = static_cast<std::size_t>( mSampleCount ) * 2;
  mSoundBuffer[index] = static_cast<std::int16_t>( static_cast<std::int32_t>( left ) );
  mSoundBuffer[index + 1] = static_cast<std::int16_t>( static_cast<std::int32_t>( right ) );
  mSampleCount++;

  if ( mSampleCount >= MAX_SAMPLES )
  {
    playQueuedAudio();
  }
}

void Apu::run()
{
  mClockCounter++;

  mCh1->exec();
  mCh2->exec();
  mCh3->exec();
  mCh4->exec();

  auto const clock = static_cast<std::uint8_t>( ( mClockCounter - 1 ) & 0x7FU );
  if ( clock >= 116 )
  {
    switch ( clock )
    {
    case 116:
      mSystem->getDmaController()->processSoundDma();
      break;
    case 122:
      mHyperVoice->exec();
      break;
    case 123:
      mCh1->updateOutput();
      break;
    case 124:
      mCh2->updateOutput();
      break;
    case 125:
      mCh3->updateOutput();
      break;
    case 126:
      mCh4->updateOutput();
      break;
    case 127:
      updateOutput();
      break;
    default:
      break;
    }
  }
}

std::uint8_t Apu::read( std::uint16_t port )
{
  switch ( port )
  {
  case 0x6A:
  case 0x6B:
    return mHyperVoice->read( port );

  case 0x80:
    return getBits<0>( mState.ch1.frequency );
  case 0x81:
    return getBits<8>( mState.ch1.frequency );
  case 0x82:
    return getBits<0>( mState.ch2.frequency );
  case 0x83:
    return getBits<8>( mState.ch2.frequency );
  case 0x84:
    return getBits<0>( mState.ch3.frequency );
  case 0x85:
    return getBits<8>( mState.ch3.frequency );
  case 0x86:
    return getBits<0>( mState.ch4.frequency );
  case 0x87:
    return getBits<8>( mState.ch4.frequency );

  case 0x88:
    return mState.ch1.getVolume();
  case 0x89:
    return mState.ch2.getVolume();
  case 0x8A:
    return mState.ch3.getVolume();
  case 0x8B:
    return mState.ch4.getVolume();

  case 0x8C:
    return static_cast<std::uint8_t>( mState.ch3.sweepValue );
  case 0x8D:
    return mState.ch3.sweepPeriod;
  case 0x8E:
    return static_cast<std::uint8_t>( mState.ch4.tapMode | ( static_cast<unsigned>( mState.ch4.lfsrEnabled ) << 4U ) );

  case 0x8F:
    return static_cast<std::uint8_t>( mState.waveTableAddress >> 6U );

  case 0x90:
    return static_cast<std::uint8_t>(
        static_cast<unsigned>( mState.ch1.enabled ) | ( static_cast<unsigned>( mState.ch2.enabled ) << 1U ) |
        ( static_cast<unsigned>( mState.ch3.enabled ) << 2U ) | ( static_cast<unsigned>( mState.ch4.enabled ) << 3U ) |
        ( static_cast<unsigned>( mState.ch2.pcmEnabled ) << 5U ) |
        ( static_cast<unsigned>( mState.ch3.sweepEnabled ) << 6U ) |
        ( static_cast<unsigned>( mState.ch4.noiseEnabled ) << 7U ) );

  case 0x91:
    return static_cast<std::uint8_t>( static_cast<unsigned>( mState.speakerEnabled ) |
                                      ( static_cast<unsigned>( mState.speakerVolume ) << 1U ) |
                                      ( static_cast<unsigned>( mState.headphoneEnabled ) << 3U ) |
                                      ( mSystem->getConfig().audioMode == AudioMode::HEADPHONES ? 0x80U : 0U ) );

  case 0x92:
    return getBits<0>( mState.ch4.lfsr );
  case 0x93:
    return getBits<8>( mState.ch4.lfsr );

  case 0x94:
    return static_cast<std::uint8_t>( static_cast<unsigned>( mState.ch2.maxPcmVolumeRight ) |
                                      ( static_cast<unsigned>( mState.ch2.halfPcmVolumeRight ) << 1U ) |
                                      ( static_cast<unsigned>( mState.ch2.maxPcmVolumeLeft ) << 2U ) |
                                      ( static_cast<unsigned>( mState.ch2.halfPcmVolumeLeft ) << 3U ) );

  case 0x95:
    return mState.soundTest;

  case 0x96:
    return getBits<0>( getApuOutput( true ) );
  case 0x97:
    return getBits<8>( getApuOutput( true ) );
  case 0x98:
    return getBits<0>( getApuOutput( false ) );
  case 0x99:
    return getBits<8>( getApuOutput( false ) );
  case 0x9A:
    return getBits<0>( getApuOutput( false ) + getApuOutput( true ) );
  case 0x9B:
    return getBits<8>( getApuOutput( false ) + getApuOutput( true ) );

  case 0x9E:
    if ( mSystem->getModel() != Model::MONOCHROME )
    {
      return mState.masterVolume;
    }
    break;

  default:
    break;
  }

  return mSystem->getMemoryManager()->getUnmappedPort();
}

void Apu::write( std::uint16_t port, std::uint8_t value )
{
  switch ( port )
  {
  case 0x64:
  case 0x65:
  case 0x66:
  case 0x67:
  case 0x69:
  case 0x6A:
  case 0x6B:
    mHyperVoice->write( port, value );
    break;

  case 0x80:
    setBits<0>( mState.ch1.frequency, value );
    break;
  case 0x81:
    setBits<8>( mState.ch1.frequency, value & 0x07U );
    break;
  case 0x82:
    setBits<0>( mState.ch2.frequency, value );
    break;
  case 0x83:
    setBits<8>( mState.ch2.frequency, value & 0x07U );
    break;
  case 0x84:
    setBits<0>( mState.ch3.frequency, value );
    break;
  case 0x85:
    setBits<8>( mState.ch3.frequency, value & 0x07U );
    break;
  case 0x86:
    setBits<0>( mState.ch4.frequency, value );
    break;
  case 0x87:
    setBits<8>( mState.ch4.frequency, value & 0x07U );
    break;

  case 0x88:
    mState.ch1.setVolume( value );
    break;
  case 0x89:
    mState.ch2.setVolume( value );
    break;
  case 0x8A:
    mState.ch3.setVolume( value );
    break;
  case 0x8B:
    mState.ch4.setVolume( value );
    break;

  case 0x8C:
    mState.ch3.sweepValue = static_cast<std::int8_t>( value );
    break;
  case 0x8D:
    mState.ch3.sweepPeriod = value & 0x1FU;
    break;

  case 0x8E:
  {
    static constexpr std::array<std::uint8_t, 8> TAP_SHIFTS{ 14, 10, 13, 4, 8, 6, 9, 11 };
    mState.ch4.tapMode = value & 0x07U;
    mState.ch4.tapShift = TAP_SHIFTS[mState.ch4.tapMode];
    if ( ( value & 0x08U ) != 0 )
    {
      mState.ch4.lfsr = 0;
    }
    mState.ch4.lfsrEnabled = ( value & 0x10U ) != 0;
    break;
  }

  case 0x8F:
    mState.waveTableAddress = static_cast<std::uint16_t>( value << 6U );
    break;

  case 0x90:
    mState.ch1.enabled = ( value & 0x01U ) != 0;
    mState.ch2.enabled = ( value & 0x02U ) != 0;
    mState.ch3.enabled = ( value & 0x04U ) != 0;
    mState.ch4.enabled = ( value & 0x08U ) != 0;

    mState.ch2.pcmEnabled = ( value & 0x20U ) != 0;
    mState.ch3.sweepEnabled = ( value & 0x40U ) != 0;
    mState.ch4.noiseEnabled = ( value & 0x80U ) != 0;
    break;

  case 0x91:
    mState.speakerEnabled = ( value & 0x01U ) != 0;
    mState.speakerVolume = static_cast<std::uint8_t>( ( value & 0x06U ) >> 1U );
    mState.headphoneEnabled = ( value & 0x08U ) != 0;
    break;

  case 0x94:
    mState.ch2.maxPcmVolumeRight = ( value & 0x01U ) != 0;
    mState.ch2.halfPcmVolumeRight = ( value & 0x02U ) != 0;
    mState.ch2.maxPcmVolumeLeft = ( value & 0x04U ) != 0;
    mState.ch2.halfPcmVolumeLeft = ( value & 0x08U ) != 0;
    break;

  case 0x95:
    mState.soundTest = value;

    mState.holdChannels = ( value & 0x01U ) != 0;
    mState.ch3.useSweepCpuClock = ( value & 0x02U ) != 0;
    mState.ch4.holdLfsr = static_cast<std::uint8_t>( ( value & 0x0CU ) >> 2U );
    // todows bit 4?
    mState.forceOutputCh2Voice = ( value & 0x20U ) != 0;
    mState.forceOutput2 = ( value & 0x40U ) != 0;
    mState.forceOutput4 = ( value & 0x80U ) != 0;
    break;

  case 0x9E:
    if ( mSystem->getModel() != Model::MONOCHROME )
    {
      mState.internalMasterVolume = value & 0x03U;
      mState.masterVolume = value & 0x03U;
    }
    break;

  default:
    break;
  }
}

std::uint16_t Apu::getApuOutput( bool forRight )
{
  if ( forRight )
  {
    return static_cast<std::uint16_t>( mState.ch1.rightOutput + mState.ch2.rightOutput + mState.ch3.rightOutput +
                                       mState.ch4.rightOutput );
  }
  return static_cast<std::uint16_t>( mState.ch1.leftOutput + mState.ch2.leftOutput + mState.ch3.leftOutput +
                                     mState.ch4.leftOutput );
}

} // namespace ws::machine
