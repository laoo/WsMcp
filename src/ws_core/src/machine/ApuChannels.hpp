#pragma once

// The APU's four channels and the HyperVoice. Channel 1 plays a wavetable;
// channel 2 a wavetable or the PCM voice; channel 3 a wavetable with a
// frequency sweep; channel 4 a wavetable or noise. Each works on its part of
// the ApuState the Apu owns. Ported from Mesen2's Core/WS/APU/WsApuCh1.h,
// WsApuCh2.h, WsApuCh3.h, WsApuCh4.h and WsHyperVoice.h at
// b9fa69ddc6d0a331fb103fdb5eef6904305703c2.

#include "Apu.hpp"

#include <cstdint>

namespace ws::machine
{

class ApuCh1
{
public:
  ApuCh1( Apu* apu, ApuCh1State& state ) : mApu{ apu }, mState{ &state } {}

  void exec()
  {
    if ( !mState->enabled )
    {
      return;
    }

    if ( mState->timer == 0 )
    {
      mState->timer = static_cast<std::uint16_t>( 2047 - mState->frequency );
      mState->samplePosition = static_cast<std::uint8_t>( ( mState->samplePosition + 1 ) & 0x1F );
    }
    else
    {
      mState->timer--;
    }
  }

  void updateOutput()
  {
    if ( !mState->enabled )
    {
      mState->leftOutput = 0;
      mState->rightOutput = 0;
      return;
    }

    std::uint8_t const sample = mApu->readSample( 0, mState->samplePosition );

    mState->leftOutput = static_cast<std::uint8_t>( mState->leftVolume * sample );
    mState->rightOutput = static_cast<std::uint8_t>( mState->rightVolume * sample );
  }

private:
  Apu* mApu;
  ApuCh1State* mState;
};

class ApuCh2
{
public:
  ApuCh2( Apu* apu, ApuCh2State& state ) : mApu{ apu }, mState{ &state } {}

  void exec()
  {
    if ( !mState->enabled )
    {
      return;
    }

    if ( mState->timer == 0 )
    {
      mState->timer = static_cast<std::uint16_t>( 2047 - mState->frequency );
      mState->samplePosition = static_cast<std::uint8_t>( ( mState->samplePosition + 1 ) & 0x1F );
    }
    else
    {
      mState->timer--;
    }
  }

  void updateOutput()
  {
    if ( mState->pcmEnabled )
    {
      std::uint8_t const sample = mState->getVolume();
      mState->leftOutput =
          static_cast<std::uint8_t>( sample >> pcmShift( mState->maxPcmVolumeLeft, mState->halfPcmVolumeLeft ) );
      mState->rightOutput =
          static_cast<std::uint8_t>( sample >> pcmShift( mState->maxPcmVolumeRight, mState->halfPcmVolumeRight ) );
    }
    else if ( mState->enabled )
    {
      std::uint8_t const sample = mApu->readSample( 1, mState->samplePosition );
      mState->leftOutput = static_cast<std::uint8_t>( mState->leftVolume * sample );
      mState->rightOutput = static_cast<std::uint8_t>( mState->rightVolume * sample );
    }
    else
    {
      mState->leftOutput = 0;
      mState->rightOutput = 0;
    }
  }

private:
  /// Full, half, or nothing: a shift of 8 leaves none of the 8-bit sample.
  static int pcmShift( bool max, bool half )
  {
    if ( max )
    {
      return 0;
    }
    return half ? 1 : 8;
  }

  Apu* mApu;
  ApuCh2State* mState;
};

class ApuCh3
{
public:
  ApuCh3( Apu* apu, ApuCh3State& state ) : mApu{ apu }, mState{ &state } {}

  void exec()
  {
    if ( !mState->enabled )
    {
      return;
    }

    if ( mState->sweepEnabled )
    {
      mState->sweepScaler++;
      if ( mState->useSweepCpuClock || mState->sweepScaler >= 0x2000 )
      {
        mState->sweepScaler = 0;

        if ( mState->sweepTimer == 0 )
        {
          mState->sweepTimer = mState->sweepPeriod;
          mState->frequency = static_cast<std::uint16_t>( ( mState->frequency + mState->sweepValue ) & 0xFFF );
        }
        else
        {
          mState->sweepTimer--;
        }
      }
    }

    if ( mState->timer == 0 )
    {
      mState->timer = static_cast<std::uint16_t>( 2047 - mState->frequency );
      mState->samplePosition = static_cast<std::uint8_t>( ( mState->samplePosition + 1 ) & 0x1F );
    }
    else
    {
      mState->timer--;
    }
  }

  void updateOutput()
  {
    if ( !mState->enabled )
    {
      mState->leftOutput = 0;
      mState->rightOutput = 0;
      return;
    }

    std::uint8_t const sample = mApu->readSample( 2, mState->samplePosition );

    mState->leftOutput = static_cast<std::uint8_t>( mState->leftVolume * sample );
    mState->rightOutput = static_cast<std::uint8_t>( mState->rightVolume * sample );
  }

private:
  Apu* mApu;
  ApuCh3State* mState;
};

class ApuCh4
{
public:
  ApuCh4( Apu* apu, ApuCh4State& state ) : mApu{ apu }, mState{ &state } {}

  void exec()
  {
    if ( !mState->enabled )
    {
      return;
    }

    if ( mState->timer == 0 )
    {
      mState->timer = static_cast<std::uint16_t>( 2047 - mState->frequency );
      mState->samplePosition = static_cast<std::uint8_t>( ( mState->samplePosition + 1 ) & 0x1F );

      if ( mState->lfsrEnabled )
      {
        auto const newBit = static_cast<std::uint16_t>(
            ( ( mState->lfsr >> 7U ) ^ ( mState->lfsr >> mState->tapShift ) ^ 0x01U ) & 0x01U );
        mState->lfsr =
            static_cast<std::uint16_t>( ( ( static_cast<unsigned>( mState->lfsr ) << 1U ) | newBit ) & 0x7FFFU );
      }
    }
    else
    {
      mState->timer--;
    }
  }

  void updateOutput()
  {
    if ( !mState->enabled )
    {
      mState->leftOutput = 0;
      mState->rightOutput = 0;
      return;
    }

    std::uint8_t sample = 0;
    if ( mState->noiseEnabled )
    {
      sample = ( mState->lfsr & 0x01U ) != 0 ? 0x0F : 0;
    }
    else
    {
      sample = mApu->readSample( 3, mState->samplePosition );
    }

    mState->leftOutput = static_cast<std::uint8_t>( mState->leftVolume * sample );
    mState->rightOutput = static_cast<std::uint8_t>( mState->rightVolume * sample );
  }

private:
  Apu* mApu;
  ApuCh4State* mState;
};

class HyperVoice
{
public:
  explicit HyperVoice( ApuHyperVoiceState& state ) : mState{ &state } {}

  std::int16_t convertSampleToOutput( std::uint8_t sample )
  {
    std::int32_t output = 0;
    switch ( mState->scalingMode )
    {
    case HyperVoiceScalingMode::UNSIGNED:
      output = sample << 8;
      break;
    case HyperVoiceScalingMode::UNSIGNED_NEGATED:
      output = static_cast<std::int32_t>( 0xFFFF0000U | ( static_cast<std::uint32_t>( sample ) << 8U ) );
      break;
    case HyperVoiceScalingMode::SIGNED:
      output = static_cast<std::int32_t>( static_cast<std::int8_t>( sample ) ) * 256;
      break;
    case HyperVoiceScalingMode::NONE:
      output = static_cast<std::int16_t>( sample << 8 );
      break;
    }

    if ( mState->scalingMode != HyperVoiceScalingMode::NONE )
    {
      output >>= mState->shift;
    }
    // Mesen2 returns the 32-bit value as an int16_t: the bits above 15 are
    // dropped, so an unsigned sample of 0x80 or more comes out negative.
    return static_cast<std::int16_t>( output );
  }

  void exec()
  {
    if ( !mState->enabled )
    {
      return;
    }

    if ( mState->timer == 0 )
    {
      mState->timer = mState->divisor;

      mState->leftOutput = convertSampleToOutput( mState->leftSample );
      mState->rightOutput = convertSampleToOutput( mState->rightSample );
    }
    else
    {
      mState->timer--;
    }
  }

  [[nodiscard]] std::uint8_t read( std::uint16_t port ) const
  {
    switch ( port )
    {
    case 0x6A:
      return mState->controlLow;
    case 0x6B:
      return mState->controlHigh;
    default:
      break;
    }

    // Unreachable
    return 0;
  }

  void writeStereoInput( std::uint8_t sample )
  {
    if ( mState->updateRightValue )
    {
      mState->rightSample = sample;
    }
    else
    {
      mState->leftSample = sample;
    }
    mState->updateRightValue = !mState->updateRightValue;
  }

  void writeDma( std::uint8_t sample )
  {
    switch ( mState->channelMode )
    {
    case HyperVoiceChannelMode::STEREO:
      writeStereoInput( sample );
      break;

    case HyperVoiceChannelMode::MONO_LEFT:
      mState->leftSample = sample;
      break;

    case HyperVoiceChannelMode::MONO_RIGHT:
      mState->rightSample = sample;
      break;

    case HyperVoiceChannelMode::MONO_BOTH:
      mState->leftSample = sample;
      mState->rightSample = sample;
      break;
    }
  }

  void write( std::uint16_t port, std::uint8_t value )
  {
    switch ( port )
    {
    case 0x64:
      mState->leftOutput = setLowByte( mState->leftOutput, value );
      break;
    case 0x65:
      mState->leftOutput = setHighByte( mState->leftOutput, value );
      break;
    case 0x66:
      mState->rightOutput = setLowByte( mState->rightOutput, value );
      break;
    case 0x67:
      mState->rightOutput = setHighByte( mState->rightOutput, value );
      break;

    case 0x69:
      writeStereoInput( value );
      break;

    case 0x6A:
    {
      mState->shift = value & 0x03U;
      mState->scalingMode = static_cast<HyperVoiceScalingMode>( ( value >> 2U ) & 0x03U );
      mState->enabled = ( value & 0x80U ) != 0;
      auto const dividor = static_cast<std::uint8_t>( ( value >> 4U ) & 0x07U );
      switch ( dividor )
      {
      default:
        mState->divisor = dividor;
        break;
      case 6:
        mState->divisor = 7;
        break;
      case 7:
        mState->divisor = 11;
        break;
      }
      mState->controlLow = value;
      break;
    }

    case 0x6B:
    {
      bool const reset = ( value & 0x10U ) != 0;
      if ( reset )
      {
        mState->updateRightValue = false;
      }
      mState->channelMode = static_cast<HyperVoiceChannelMode>( ( value >> 5U ) & 0x03U );
      mState->controlHigh = value & 0x6FU;
      break;
    }

    default:
      break;
    }
  }

private:
  static std::int16_t setLowByte( std::int16_t dst, std::uint8_t value )
  {
    return static_cast<std::int16_t>( ( static_cast<std::uint16_t>( dst ) & 0xFF00U ) | value );
  }

  static std::int16_t setHighByte( std::int16_t dst, std::uint8_t value )
  {
    return static_cast<std::int16_t>( ( static_cast<std::uint16_t>( dst ) & 0x00FFU ) |
                                      ( static_cast<unsigned>( value ) << 8U ) );
  }

  ApuHyperVoiceState* mState;
};

} // namespace ws::machine
