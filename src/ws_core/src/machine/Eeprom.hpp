#pragma once

// The 93Cxx serial EEPROMs: the console's own, ports $BA-$BE, and a
// cartridge's, ports $C4-$C8. Ported from Mesen2's Core/WS/WsEeprom.h and
// WsEeprom.cpp at b9fa69ddc6d0a331fb103fdb5eef6904305703c2.

#include "System.hpp"

#include <cstdint>
#include <span>

namespace ws::machine
{

enum class EepromCommand : std::uint8_t
{
  READ,
  WRITE,
  ERASE,
  WRITE_DISABLE,
  WRITE_ENABLE,
  ERASE_ALL,
  WRITE_ALL,
  UNKNOWN
};

class Eeprom
{
public:
  /// An EEPROM of `data`, which the System keeps. The internal one is left
  /// writable where there is no boot ROM to protect it, as Mesen2 has it.
  Eeprom( System& system, std::span<std::uint8_t> data, bool isInternal, bool bootRom );

  EepromState& getState()
  {
    return mState;
  }

  /// The console's EEPROM as it leaves the factory: its name, and the
  /// Color's volume at its loudest.
  static void initInternalEepromData( Model model, std::span<std::uint8_t> data );

  void writePort( std::uint8_t port, std::uint8_t value );
  std::uint8_t readPort( std::uint8_t port );

  /// Ends the command under way when its time has come.
  void run();

  template <class Archive>
  void serialize( Archive& archive )
  {
    archive( mState );
  }

private:
  [[nodiscard]] EepromCommand getCommand() const;
  [[nodiscard]] EepromSize getSize() const;
  [[nodiscard]] std::uint16_t getCommandAddress() const;

  void writeValue( std::uint16_t addr, std::uint16_t value );

  System* mSystem;
  std::span<std::uint8_t> mData;
  bool mIsInternal;
  EepromState mState{};
};

} // namespace ws::machine
