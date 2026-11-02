#pragma once

// The general DMA and the sound DMA of the Color. Ported from Mesen2's
// Core/WS/WsDmaController.h and WsDmaController.cpp at
// b9fa69ddc6d0a331fb103fdb5eef6904305703c2.

#include "System.hpp"

#include <cstdint>

namespace ws::machine
{

class DmaController
{
public:
  explicit DmaController( System& system );

  void runGeneralDma();
  void processSoundDma();

  DmaControllerState& getState()
  {
    return mState;
  }

  std::uint8_t readPort( std::uint16_t port );
  void writePort( std::uint16_t port, std::uint8_t value );

  template <class Archive>
  void serialize( Archive& archive )
  {
    archive( mState );
  }

private:
  System* mSystem;
  DmaControllerState mState{};
};

} // namespace ws::machine
