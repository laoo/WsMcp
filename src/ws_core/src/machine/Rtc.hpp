#pragma once

// The Bandai 2003's RTC interface, ports $CA and $CB, and the Seiko S-3511A
// behind it. Mesen2 has neither at b9fa69ddc6d0a331fb103fdb5eef6904305703c2,
// so this part is not ported from it
// (docs/decisions/0015-the-rtc-follows-the-wsdev-wiki.md). The 2003's side
// follows the WSdev wiki's "Bandai 2003" page and asiekierka's
// ws-test-suite rtc/mapper, which is held to it; the S-3511A's registers,
// commands and calendar follow the wiki's "Real-Time Clock" page; where it is
// silent, ares's ares/ws/cartridge/rtc.cpp at
// 4307bf2eff6e4f9a277fdc6f9d4f4de177b3f932 (ISC) is followed.

#include "ws/machine/Types.hpp"

#include <array>
#include <cstdint>
#include <span>

namespace ws::machine
{

class System;

class Rtc
{
public:
  /// The registers a cartridge's battery keeps: the date and time, the
  /// configuration and the alarm.
  static constexpr std::size_t REGISTERS = 10;

  Rtc( System& system, RtcState& state );

  /// The S-3511A as a new battery leaves it.
  void powerOn();

  std::uint8_t readControl();
  void writeControl( std::uint8_t value );
  std::uint8_t readData();
  void writeData( std::uint8_t value );

  /// Brings the clock and the command running up to now, and raises the
  /// cartridge's interrupt where the S-3511A asks for it.
  void run();

  /// What the battery keeps, to be put back with restore().
  [[nodiscard]] std::array<std::uint8_t, REGISTERS> registers() const;
  /// The registers `saved` holds, with the clock run on by `secondsAway`, the
  /// time the console was off. A save of another size is ignored.
  void restore( std::span<std::uint8_t const> saved, std::uint64_t secondsAway );

private:
  /// Ends the command: the 2003's busy bit drops, and its ready bit is set.
  void finish();
  /// Moves the command on by the byte that went out or came in at `at`.
  void byteThrough( std::uint64_t at );
  /// What a command reads from the S-3511A, taken when it starts.
  void fetchPayload();
  /// What a command wrote to the S-3511A, applied when it ends.
  void applyPayload();
  void reset();

  void tickSecond();
  /// The days of the month the date stands in.
  [[nodiscard]] std::uint8_t daysInMonth() const;
  void nextDay();
  void nextMonth();
  /// The date and time `bytes` hold from index `first` on, each checked as
  /// the S-3511A checks a write.
  void writeDate( std::span<std::uint8_t const> bytes );
  void writeTime( std::span<std::uint8_t const> bytes );
  [[nodiscard]] bool irqAsserted() const;

  System* mSystem;
  RtcState* mState;
};

} // namespace ws::machine
