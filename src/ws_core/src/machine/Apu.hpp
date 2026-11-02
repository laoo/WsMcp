#pragma once

// The sound: four wavetable channels, the PCM voice and the HyperVoice.
// Ported from Mesen2's Core/WS/APU/WsApu.h and WsApu.cpp at
// b9fa69ddc6d0a331fb103fdb5eef6904305703c2.

#include "System.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <numbers>

namespace ws::machine
{

class ApuCh1;
class ApuCh2;
class ApuCh3;
class ApuCh4;
class HyperVoice;

class Apu
{
public:
  /// The rate of the samples handed to System::audioDone(), stereo.
  static constexpr int SAMPLE_RATE = 24000;

  explicit Apu( System& system );
  ~Apu();

  Apu( Apu const& ) = delete;
  Apu& operator=( Apu const& ) = delete;
  Apu( Apu&& ) = delete;
  Apu& operator=( Apu&& ) = delete;

  void changeMasterVolume();

  /// Hands the sound made since the last call to System::audioDone().
  void playQueuedAudio();
  void writeDma( bool forHyperVoice, std::uint8_t sampleValue );

  ApuState& getState();
  [[nodiscard]] ApuState const& getState() const;

  [[nodiscard]] std::uint8_t getMasterVolume() const;

  std::uint8_t readSample( std::uint8_t ch, std::uint8_t pos );
  /// A CPU cycle.
  void run();

  std::uint8_t read( std::uint16_t port );
  void write( std::uint16_t port, std::uint8_t value );

  template <class Archive>
  void serialize( Archive& archive )
  {
    archive( mState );
    archive( mClockCounter );
    mFilterL.serialize( archive );
    mFilterR.serialize( archive );
    archive( mSampleCount );
    archive( mSoundBuffer );
  }

private:
  /// The samples queued before they are handed over, each a stereo pair.
  static constexpr int MAX_SAMPLES = 256;

  /// Mesen2's Utilities/Audio/OnePoleLowPassFilter.h: the low pass whose
  /// output is taken off the sound to remove its DC offset.
  class OnePoleLowPassFilter
  {
  public:
    void setCutoffFrequency( double freq, double sampleRate )
    {
      mB1 = std::exp( -2.0 * std::numbers::pi * ( freq / sampleRate ) );
      mA0 = 1.0 - mB1;
    }

    double process( double nextSample )
    {
      return mPrevSample = ( nextSample * mA0 ) + ( mPrevSample * mB1 );
    }

    template <class Archive>
    void serialize( Archive& archive )
    {
      archive( mA0 );
      archive( mB1 );
      archive( mPrevSample );
    }

  private:
    double mA0 = 1;
    double mB1 = 0;
    double mPrevSample = 0;
  };

  void updateOutput();
  std::uint16_t getApuOutput( bool forRight );

  System* mSystem;
  ApuState mState{};

  std::unique_ptr<ApuCh1> mCh1;
  std::unique_ptr<ApuCh2> mCh2;
  std::unique_ptr<ApuCh3> mCh3;
  std::unique_ptr<ApuCh4> mCh4;
  std::unique_ptr<HyperVoice> mHyperVoice;

  OnePoleLowPassFilter mFilterL;
  OnePoleLowPassFilter mFilterR;

  std::array<std::int16_t, static_cast<std::size_t>( MAX_SAMPLES ) * 2> mSoundBuffer{};

  std::uint32_t mClockCounter = 0;
  std::uint16_t mSampleCount = 0;
};

} // namespace ws::machine
