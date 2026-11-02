#pragma once

// The cartridge's mapper, the Bandai 2001 and 2003 as far as their banks:
// ports $C0-$C3, and the EEPROM's ports $C4-$C8. Ported from Mesen2's
// Core/WS/Carts/WsCart.h and WsCart.cpp at b9fa69ddc6d0a331fb103fdb5eef6904305703c2,
// which has neither the RTC nor the flash yet.

#include "System.hpp"

#include <cstdint>

namespace ws::machine
{

class Cart
{
public:
  explicit Cart( System& system );

  void refreshMappings();

  CartState& getState()
  {
    return mState;
  }

  [[nodiscard]] Eeprom* getEeprom() const;

  std::uint8_t readPort( std::uint16_t port );
  void writePort( std::uint16_t port, std::uint8_t value );

  template <class Archive>
  void serialize( Archive& archive )
  {
    archive( mState );
  }

private:
  System* mSystem;
  CartState mState{};
};

} // namespace ws::machine
