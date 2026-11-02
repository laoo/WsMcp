#pragma once

// The buttons, as the keypad port $B5 reads them, and the key interrupt.
// Ported from Mesen2's Core/WS/WsControlManager.h and WsControlManager.cpp at
// b9fa69ddc6d0a331fb103fdb5eef6904305703c2, with its WsController reduced to
// the mask of Button bits System::setButtons() holds.

#include "System.hpp"

#include <cstdint>

namespace ws::machine
{

class ControlManager
{
public:
  explicit ControlManager( System& system );

  ControlManagerState& getState()
  {
    return mState;
  }

  /// The buttons held from now on, Button bits.
  void setButtons( std::uint16_t buttons );
  [[nodiscard]] std::uint16_t getButtons() const;

  /// Mesen2's UpdateInputState, at the end of each frame: a button newly
  /// pressed asks for the key interrupt, and SOUND steps the volume.
  void updateInputState();

  /// Raises the key interrupt asked for, on the line the PPU calls it from.
  void triggerKeyIrq();

  [[nodiscard]] std::uint8_t read() const;
  void write( std::uint8_t value );

  [[nodiscard]] bool isSoundPressed() const;

  template <class Archive>
  void serialize( Archive& archive )
  {
    archive( mState );
    archive( mPrevInput );
    archive( mSoundButtonPressed );
    archive( mNeedKeyIrq );
  }

private:
  [[nodiscard]] bool isPressed( Button button ) const;

  System* mSystem;
  ControlManagerState mState{};
  std::uint16_t mButtons{};

  std::uint8_t mPrevInput{};
  bool mSoundButtonPressed{};
  bool mNeedKeyIrq{};
};

} // namespace ws::machine
