#include "Rtc.hpp"

#include "MemoryManager.hpp"
#include "System.hpp"

#include <algorithm>

namespace ws::machine
{

namespace
{

constexpr std::uint64_t CYCLES_PER_SECOND = CPU_CLOCK_RATE;
constexpr std::uint64_t SECONDS_PER_DAY = 86'400;
/// The most days a restore runs the clock on: a century, past which the
/// two-digit year has wrapped anyway.
constexpr std::uint64_t MAX_DAYS_AWAY = 36'525;

/// The 2003 relays the S-3511A's serial clock at 384 kHz: 8 of the CPU's
/// cycles a bit, 64 a byte.
constexpr std::uint64_t BYTE_CYCLES = 64;

/// The payload bytes of commands $10-$1B; the odd ones read, the even ones
/// write. $1C-$1F are no commands.
constexpr std::array<std::uint8_t, 12> PAYLOAD_SIZES{ 0, 0, 1, 1, 7, 7, 3, 3, 2, 2, 2, 2 };

constexpr std::uint8_t BUSY = 0x10;

constexpr std::uint8_t POWER_FAILURE = 0x80;
constexpr std::uint8_t HOURS_24 = 0x40;
constexpr std::uint8_t ALARM = 0x20;
constexpr std::uint8_t PER_MINUTE = 0x08;
constexpr std::uint8_t FREQUENCY = 0x02;
constexpr std::uint8_t CONFIG_WRITABLE = HOURS_24 | ALARM | PER_MINUTE | FREQUENCY;
constexpr std::uint8_t PM = 0x80;

enum DateTime : std::uint8_t
{
  YEAR,
  MONTH,
  DAY,
  WEEKDAY,
  HOUR,
  MINUTE,
  SECOND
};

bool isBcd( std::uint8_t value )
{
  return ( value & 0x0FU ) <= 9 && ( value >> 4U ) <= 9;
}

std::uint8_t bcdIncrement( std::uint8_t value )
{
  return static_cast<std::uint8_t>( ( value & 0x0FU ) >= 9 ? ( value & 0xF0U ) + 0x10U : value + 1U );
}

unsigned fromBcd( std::uint8_t value )
{
  return ( ( value >> 4U ) * 10U ) + ( value & 0x0FU );
}

} // namespace

Rtc::Rtc( System& system, RtcState& state ) : mSystem{ &system }, mState{ &state } {}

void Rtc::powerOn()
{
  reset();
  mState->config = POWER_FAILURE | FREQUENCY;
  mState->alarm = { 0x00, 0x80 };
  mState->lastClock = mSystem->getMasterClock();
}

void Rtc::reset()
{
  mState->dateTime = { 0x00, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00 };
  mState->config = 0;
  mState->alarm = { 0x00, 0x00 };
}

std::uint8_t Rtc::readControl()
{
  run();
  return static_cast<std::uint8_t>( ( mState->ready ? 0x80U : 0U ) | mState->command );
}

void Rtc::writeControl( std::uint8_t value )
{
  run();
  // A new command clears Ready, whatever it is; bit 7 cannot set it.
  mState->ready = false;
  mState->command = value & 0x1FU;
  mState->phase = RtcPhase::IDLE;
  // Without bit 4 nothing starts. A command past $1B stops at once, but its
  // bit 4 stays as written: it reads as busy for ever.
  if ( ( mState->command & BUSY ) == 0 || ( mState->command & 0x0FU ) >= PAYLOAD_SIZES.size() )
  {
    return;
  }
  mState->index = 0;
  mState->phase = RtcPhase::COMMAND;
  mState->phaseEnd = mSystem->getMasterClock() + BYTE_CYCLES;
  if ( ( mState->command & 0x01U ) != 0 )
  {
    fetchPayload();
  }
}

std::uint8_t Rtc::readData()
{
  run();
  std::uint8_t const value = mState->data;
  if ( mState->phase == RtcPhase::WAIT_READ )
  {
    mState->ready = false;
    mState->phase = RtcPhase::PAYLOAD;
    mState->phaseEnd = mSystem->getMasterClock() + BYTE_CYCLES;
  }
  else if ( ( mState->command & BUSY ) == 0 )
  {
    mState->ready = false;
  }
  return value;
}

void Rtc::writeData( std::uint8_t value )
{
  run();
  mState->data = value;
  if ( mState->phase == RtcPhase::WAIT_WRITE )
  {
    mState->payload.at( mState->index ) = value;
    mState->ready = false;
    mState->phase = RtcPhase::PAYLOAD;
    mState->phaseEnd = mSystem->getMasterClock() + BYTE_CYCLES;
  }
  else if ( ( mState->command & BUSY ) == 0 )
  {
    mState->ready = false;
  }
}

void Rtc::run()
{
  std::uint64_t const now = mSystem->getMasterClock();
  while ( ( mState->phase == RtcPhase::COMMAND || mState->phase == RtcPhase::PAYLOAD ) && now >= mState->phaseEnd )
  {
    byteThrough( mState->phaseEnd );
  }

  std::uint64_t const total = mState->subsecond + ( now - mState->lastClock );
  mState->lastClock = now;
  mState->subsecond = static_cast<std::uint32_t>( total % CYCLES_PER_SECOND );
  for ( std::uint64_t second = total / CYCLES_PER_SECOND; second > 0; --second )
  {
    tickSecond();
  }

  // The S-3511A's /IRQ is the cartridge's interrupt line, held as long as
  // it is asserted.
  if ( irqAsserted() )
  {
    mSystem->getMemoryManager()->setIrqSource( IrqSource::CART );
  }
}

void Rtc::byteThrough( std::uint64_t at )
{
  std::uint8_t const size = PAYLOAD_SIZES.at( mState->command & 0x0FU );
  bool const reading = ( mState->command & 0x01U ) != 0;
  if ( mState->phase == RtcPhase::COMMAND )
  {
    if ( size == 0 )
    {
      applyPayload();
      finish();
      return;
    }
    // The first byte to write was put in $CB before the command started.
    if ( !reading )
    {
      mState->payload[0] = mState->data;
    }
    mState->phase = RtcPhase::PAYLOAD;
    mState->phaseEnd = at + BYTE_CYCLES;
    return;
  }

  if ( reading )
  {
    mState->data = mState->payload.at( mState->index );
  }
  if ( ++mState->index == size )
  {
    if ( !reading )
    {
      applyPayload();
    }
    finish();
    return;
  }
  mState->phase = reading ? RtcPhase::WAIT_READ : RtcPhase::WAIT_WRITE;
  mState->ready = true;
}

void Rtc::finish()
{
  mState->phase = RtcPhase::IDLE;
  mState->command &= static_cast<std::uint8_t>( ~BUSY );
  mState->ready = true;
}

void Rtc::fetchPayload()
{
  auto& payload = mState->payload;
  auto const& time = mState->dateTime;
  switch ( mState->command & 0x0FU )
  {
  case 0x3:
    payload[0] = mState->config;
    mState->config &= static_cast<std::uint8_t>( ~POWER_FAILURE );
    mState->minuteIrq = false;
    break;
  case 0x5:
    payload = time;
    break;
  case 0x7:
    payload = { time[HOUR], time[MINUTE], time[SECOND] };
    break;
  case 0x9:
    // The 2003 reads while the S-3511A takes the bytes it is sent as an
    // alarm, all ones, the line's idle level.
    mState->alarm = { 0xFF, 0xFF };
    payload = { 0xFF, 0xFF };
    break;
  default:
    // $1B: nothing answers, and the line stays high.
    payload.fill( 0xFF );
    break;
  }
}

void Rtc::applyPayload()
{
  std::span<std::uint8_t const> const payload{ mState->payload };
  switch ( mState->command & 0x0FU )
  {
  case 0x0:
  case 0x1:
    reset();
    break;
  case 0x2:
    mState->config = static_cast<std::uint8_t>( ( mState->config & POWER_FAILURE ) | ( payload[0] & CONFIG_WRITABLE ) );
    break;
  case 0x4:
    writeDate( payload.first( 4 ) );
    writeTime( payload.subspan( 4, 3 ) );
    break;
  case 0x6:
    writeTime( payload.first( 3 ) );
    break;
  case 0x8:
    mState->alarm = { payload[0], payload[1] };
    break;
  default:
    // $1A: the S-3511A takes no such command.
    break;
  }
}

void Rtc::writeDate( std::span<std::uint8_t const> bytes )
{
  auto& time = mState->dateTime;
  time[YEAR] = isBcd( bytes[0] ) ? bytes[0] : 0x00;
  auto const month = static_cast<std::uint8_t>( bytes[1] & 0x1FU );
  time[MONTH] = isBcd( month ) && month >= 0x01 && month <= 0x12 ? month : 0x01;
  auto const day = static_cast<std::uint8_t>( bytes[2] & 0x3FU );
  time[DAY] = isBcd( day ) && day >= 0x01 && day <= 0x31 ? day : 0x01;
  auto const weekday = static_cast<std::uint8_t>( bytes[3] & 0x07U );
  time[WEEKDAY] = weekday <= 6 ? weekday : 0x00;
  // A day the month does not have moves on to the first of the next.
  if ( time[DAY] > daysInMonth() )
  {
    time[DAY] = 0x01;
    nextMonth();
  }
}

void Rtc::writeTime( std::span<std::uint8_t const> bytes )
{
  auto& time = mState->dateTime;
  auto const hour = static_cast<std::uint8_t>( bytes[0] & 0x3FU );
  if ( ( mState->config & HOURS_24 ) != 0 )
  {
    // The PM bit follows the hour, whatever was written to it.
    bool const valid = isBcd( hour ) && hour <= 0x23;
    time[HOUR] = valid ? static_cast<std::uint8_t>( hour | ( hour >= 0x12 ? PM : 0U ) ) : 0x00;
  }
  else
  {
    bool const valid = isBcd( hour ) && hour <= 0x11;
    time[HOUR] = valid ? static_cast<std::uint8_t>( hour | ( bytes[0] & PM ) ) : 0x00;
  }
  auto const minute = static_cast<std::uint8_t>( bytes[1] & 0x7FU );
  time[MINUTE] = isBcd( minute ) && minute <= 0x59 ? minute : 0x00;
  // An invalid second is kept, and rolls over at the next.
  time[SECOND] = static_cast<std::uint8_t>( bytes[2] & 0x7FU );
}

void Rtc::tickSecond()
{
  auto& time = mState->dateTime;
  if ( isBcd( time[SECOND] ) && time[SECOND] < 0x59 )
  {
    time[SECOND] = bcdIncrement( time[SECOND] );
    return;
  }
  time[SECOND] = 0x00;
  mState->minuteIrq = true;
  if ( time[MINUTE] < 0x59 )
  {
    time[MINUTE] = bcdIncrement( time[MINUTE] );
    return;
  }
  time[MINUTE] = 0x00;

  auto hour = static_cast<std::uint8_t>( time[HOUR] & 0x3FU );
  bool pm = ( time[HOUR] & PM ) != 0;
  bool nextDayToo = false;
  if ( ( mState->config & HOURS_24 ) != 0 )
  {
    nextDayToo = hour >= 0x23;
    hour = nextDayToo ? 0x00 : bcdIncrement( hour );
    pm = hour >= 0x12;
  }
  else if ( hour >= 0x11 )
  {
    // 11 AM is followed by 0 PM, 11 PM by 0 AM of the next day.
    hour = 0x00;
    nextDayToo = pm;
    pm = !pm;
  }
  else
  {
    hour = bcdIncrement( hour );
  }
  time[HOUR] = static_cast<std::uint8_t>( hour | ( pm ? PM : 0U ) );
  if ( nextDayToo )
  {
    nextDay();
  }
}

std::uint8_t Rtc::daysInMonth() const
{
  static constexpr std::array<std::uint8_t, 12> DAYS{ 0x31, 0x28, 0x31, 0x30, 0x31, 0x30,
                                                      0x31, 0x31, 0x30, 0x31, 0x30, 0x31 };
  unsigned const month = std::clamp( fromBcd( mState->dateTime[MONTH] ), 1U, 12U );
  // Every fourth year is a leap year, 00 among them, as for 2000.
  if ( month == 2 && fromBcd( mState->dateTime[YEAR] ) % 4 == 0 )
  {
    return 0x29;
  }
  return DAYS.at( month - 1 );
}

void Rtc::nextDay()
{
  auto& time = mState->dateTime;
  time[WEEKDAY] = static_cast<std::uint8_t>( ( time[WEEKDAY] + 1U ) % 7U );
  if ( time[DAY] < daysInMonth() )
  {
    time[DAY] = bcdIncrement( time[DAY] );
    return;
  }
  time[DAY] = 0x01;
  nextMonth();
}

void Rtc::nextMonth()
{
  auto& time = mState->dateTime;
  if ( time[MONTH] < 0x12 )
  {
    time[MONTH] = bcdIncrement( time[MONTH] );
    return;
  }
  time[MONTH] = 0x01;
  time[YEAR] = time[YEAR] >= 0x99 ? std::uint8_t{ 0x00 } : bcdIncrement( time[YEAR] );
}

bool Rtc::irqAsserted() const
{
  std::uint8_t const config = mState->config;
  auto const& time = mState->dateTime;
  if ( ( config & PER_MINUTE ) != 0 )
  {
    // Steady: from the minute's start to its 30th second. Edge: from the
    // minute's start until the configuration is read, within its first
    // second.
    return ( config & FREQUENCY ) != 0 ? time[SECOND] < 0x30 : mState->minuteIrq && time[SECOND] == 0x00;
  }
  if ( ( config & FREQUENCY ) != 0 )
  {
    // The prescaler, 15 bits at 32768 Hz, inverted, masked by the alarm
    // register's lower 15 bits and NORed onto /IRQ.
    auto const prescaler =
        static_cast<std::uint16_t>( ( std::uint64_t{ mState->subsecond } * 32'768 ) / CYCLES_PER_SECOND );
    auto const mask = static_cast<std::uint16_t>( mState->alarm[0] | ( mState->alarm[1] << 8U ) );
    return ( ~prescaler & mask & 0x7FFFU ) != 0;
  }
  if ( ( config & ALARM ) != 0 )
  {
    // The alarm's hour carries its PM bit, which must match even in the
    // 24-hour mode.
    return ( time[HOUR] & 0xBFU ) == ( mState->alarm[0] & 0xBFU ) &&
           ( time[MINUTE] & 0x7FU ) == ( mState->alarm[1] & 0x7FU );
  }
  return false;
}

std::array<std::uint8_t, Rtc::REGISTERS> Rtc::registers() const
{
  auto const& time = mState->dateTime;
  return { time[0], time[1], time[2],        time[3],          time[4],
           time[5], time[6], mState->config, mState->alarm[0], mState->alarm[1] };
}

void Rtc::restore( std::span<std::uint8_t const> saved, std::uint64_t secondsAway )
{
  if ( saved.size() != REGISTERS )
  {
    return;
  }
  std::copy_n( saved.begin(), mState->dateTime.size(), mState->dateTime.begin() );
  mState->config = saved[7];
  mState->alarm = { saved[8], saved[9] };
  // Whole days first, then what is left a second at a time: the time of day
  // is the same a day later.
  for ( std::uint64_t day = std::min( secondsAway / SECONDS_PER_DAY, MAX_DAYS_AWAY ); day > 0; --day )
  {
    nextDay();
  }
  for ( std::uint64_t second = secondsAway % SECONDS_PER_DAY; second > 0; --second )
  {
    tickSecond();
  }
  mState->minuteIrq = false;
}

} // namespace ws::machine
