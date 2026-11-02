#pragma once

// The serial port, $B1 and $B3, with nothing at the other end of the cable:
// what is sent is kept for the protocol to read. Ported from Mesen2's
// Core/WS/WsSerial.h and WsSerial.cpp at b9fa69ddc6d0a331fb103fdb5eef6904305703c2.

#include "System.hpp"

#include <cstdint>
#include <vector>

namespace ws::machine
{

class Serial
{
public:
  explicit Serial( System& system );

  SerialState& getState()
  {
    return mState;
  }

  std::uint8_t read( std::uint16_t port );
  void write( std::uint16_t port, std::uint8_t value );

  bool hasSendIrq();

  /// The bytes sent since the last call, oldest first.
  std::vector<std::uint8_t> takeSent();

  template <class Archive>
  void serialize( Archive& archive )
  {
    archive( mState );
  }

private:
  void updateState();

  System* mSystem;
  SerialState mState{};
  std::vector<std::uint8_t> mSent;
};

} // namespace ws::machine
