#pragma once

// The horizontal and vertical blank timers. Ported from Mesen2's
// Core/WS/WsTimer.h and WsTimer.cpp at b9fa69ddc6d0a331fb103fdb5eef6904305703c2.

#include "System.hpp"

#include <cstdint>

namespace ws::machine
{

class Timer
{
public:
  explicit Timer( System& system );

  TimerState& getState()
  {
    return mState;
  }

  void tickHorizontalTimer();
  void tickVerticalTimer();

  [[nodiscard]] std::uint8_t readPort( std::uint16_t port ) const;
  void writePort( std::uint16_t port, std::uint8_t value );

  template <class Archive>
  void serialize( Archive& archive )
  {
    archive( mState );
  }

private:
  System* mSystem;
  TimerState mState{};
};

} // namespace ws::machine
