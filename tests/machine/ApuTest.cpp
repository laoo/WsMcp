#include <catch2/catch_test_macros.hpp>

// The pieces of the APU that stand without a System: the channels' timers,
// channel 3's sweep, channel 4's LFSR and the HyperVoice. A channel's output
// reads the wavetable through the Apu, so only exec() is driven here, with no
// Apu behind the channel.

#include "machine/ApuChannels.hpp"

#include <cstdint>

using namespace ws::machine;

TEST_CASE( "a channel steps its sample position each time its timer runs out", "[apu]" )
{
  ApuCh1State state{};
  state.enabled = true;
  state.frequency = 2045;
  ApuCh1 channel{ nullptr, state };

  channel.exec();
  REQUIRE( state.samplePosition == 1 );
  REQUIRE( state.timer == 2 );

  channel.exec();
  channel.exec();
  REQUIRE( state.samplePosition == 1 );
  REQUIRE( state.timer == 0 );

  channel.exec();
  REQUIRE( state.samplePosition == 2 );

  state.samplePosition = 0x1F;
  state.timer = 0;
  channel.exec();
  REQUIRE( state.samplePosition == 0 );
}

TEST_CASE( "a disabled channel stands still", "[apu]" )
{
  ApuCh2State state{};
  state.frequency = 2047;
  ApuCh2 channel{ nullptr, state };

  channel.exec();
  REQUIRE( state.samplePosition == 0 );
  REQUIRE( state.timer == 0 );
}

TEST_CASE( "channel 3's sweep adds its signed step to a 12-bit frequency", "[apu]" )
{
  ApuCh3State state{};
  state.enabled = true;
  state.sweepEnabled = true;
  state.useSweepCpuClock = true;
  state.sweepValue = -1;
  state.frequency = 0x100;
  ApuCh3 channel{ nullptr, state };

  channel.exec();
  REQUIRE( state.frequency == 0xFF );

  state.frequency = 0;
  channel.exec();
  REQUIRE( state.frequency == 0xFFF );

  SECTION( "without the CPU clock, a step every 0x2000 clocks" )
  {
    state.useSweepCpuClock = false;
    state.sweepScaler = 0;
    state.frequency = 0x10;
    state.sweepValue = 2;
    for ( int i = 0; i < 0x1FFF; ++i )
    {
      channel.exec();
    }
    REQUIRE( state.frequency == 0x10 );
    channel.exec();
    REQUIRE( state.frequency == 0x12 );
  }
}

TEST_CASE( "channel 4's LFSR shifts in the inverted XOR of bit 7 and its tap", "[apu]" )
{
  ApuCh4State state{};
  state.enabled = true;
  state.lfsrEnabled = true;
  state.frequency = 2047;
  state.tapShift = 14;
  ApuCh4 channel{ nullptr, state };

  for ( int i = 0; i < 8; ++i )
  {
    channel.exec();
  }
  REQUIRE( state.lfsr == 0xFF );

  channel.exec();
  REQUIRE( state.lfsr == 0x1FE );

  state.lfsr = 0x7FFF;
  channel.exec();
  REQUIRE( state.lfsr == 0x7FFF );
}

TEST_CASE( "the HyperVoice scales its samples by its mode and shift", "[apu]" )
{
  ApuHyperVoiceState state{};
  HyperVoice voice{ state };

  // Enabled, unsigned, no shift, no divisor; stereo.
  voice.write( 0x6A, 0x80 );
  voice.write( 0x6B, 0x10 );
  REQUIRE( voice.read( 0x6A ) == 0x80 );
  REQUIRE( voice.read( 0x6B ) == 0x00 );

  voice.writeDma( 0x40 );
  voice.writeDma( 0x80 );
  voice.exec();
  REQUIRE( state.leftOutput == 0x4000 );
  REQUIRE( state.rightOutput == -32768 );

  SECTION( "unsigned negated" )
  {
    voice.write( 0x6A, 0x80 | ( 1U << 2U ) | 1U );
    voice.exec();
    REQUIRE( state.leftOutput == -24576 );
  }

  SECTION( "signed" )
  {
    voice.write( 0x6A, 0x80 | ( 2U << 2U ) );
    voice.exec();
    REQUIRE( state.rightOutput == -32768 );
  }

  SECTION( "divisors 6 and 7 count 7 and 11" )
  {
    voice.write( 0x6A, 0x80 | ( 6U << 4U ) );
    REQUIRE( state.divisor == 7 );
    voice.write( 0x6A, 0x80 | ( 7U << 4U ) );
    REQUIRE( state.divisor == 11 );
  }

  SECTION( "mono modes write one side or both" )
  {
    voice.write( 0x6B, 0x60 );
    REQUIRE( state.channelMode == HyperVoiceChannelMode::MONO_BOTH );
    voice.writeDma( 0x12 );
    REQUIRE( state.leftSample == 0x12 );
    REQUIRE( state.rightSample == 0x12 );
  }
}
