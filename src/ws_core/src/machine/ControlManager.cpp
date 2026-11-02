#include "ControlManager.hpp"

#include "Apu.hpp"
#include "MemoryManager.hpp"

namespace ws::machine
{

ControlManager::ControlManager( System& system ) : mSystem{ &system } {}

void ControlManager::setButtons( std::uint16_t buttons )
{
  mButtons = buttons;
}

std::uint16_t ControlManager::getButtons() const
{
  return mButtons;
}

bool ControlManager::isPressed( Button button ) const
{
  return ( mButtons & ( 1U << static_cast<unsigned>( button ) ) ) != 0;
}

std::uint8_t ControlManager::read() const
{
  unsigned result = mState.inputSelect;

  if ( ( mState.inputSelect & 0x10U ) != 0 )
  {
    result |= isPressed( Button::UP2 ) ? 0x01U : 0U;
    result |= isPressed( Button::RIGHT2 ) ? 0x02U : 0U;
    result |= isPressed( Button::DOWN2 ) ? 0x04U : 0U;
    result |= isPressed( Button::LEFT2 ) ? 0x08U : 0U;
  }
  if ( ( mState.inputSelect & 0x20U ) != 0 )
  {
    result |= isPressed( Button::UP ) ? 0x01U : 0U;
    result |= isPressed( Button::RIGHT ) ? 0x02U : 0U;
    result |= isPressed( Button::DOWN ) ? 0x04U : 0U;
    result |= isPressed( Button::LEFT ) ? 0x08U : 0U;
  }
  if ( ( mState.inputSelect & 0x40U ) != 0 )
  {
    result |= isPressed( Button::START ) ? 0x02U : 0U;
    result |= isPressed( Button::A ) ? 0x04U : 0U;
    result |= isPressed( Button::B ) ? 0x08U : 0U;
  }

  return static_cast<std::uint8_t>( result );
}

void ControlManager::write( std::uint8_t value )
{
  mState.inputSelect = value & 0x70U;
}

bool ControlManager::isSoundPressed() const
{
  return isPressed( Button::SOUND );
}

void ControlManager::updateInputState()
{
  std::uint8_t const newInput = read();
  if ( ( mPrevInput | newInput ) > mPrevInput )
  {
    // Trigger IRQ (on scanline 144) whenever any extra bits are set compared to the previous input
    mNeedKeyIrq = true;
  }
  mPrevInput = newInput;

  bool const soundButtonPressed = isSoundPressed();
  if ( soundButtonPressed && !mSoundButtonPressed )
  {
    mSystem->getApu()->changeMasterVolume();
  }
  mSoundButtonPressed = soundButtonPressed;
}

void ControlManager::triggerKeyIrq()
{
  if ( mNeedKeyIrq )
  {
    mSystem->getMemoryManager()->setIrqSource( IrqSource::KEY_PRESSED );
    mNeedKeyIrq = false;
  }
}

} // namespace ws::machine
