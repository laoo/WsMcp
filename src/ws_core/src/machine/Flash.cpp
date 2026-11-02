#include "Flash.hpp"

#include "MemoryManager.hpp"
#include "System.hpp"

#include <algorithm>
#include <array>
#include <bit>

namespace ws::machine
{

namespace
{

/// The datasheet's typical times, in cycles of the 3.072 MHz clock: a byte
/// programmed in 8 us; the 50 us an erase waits for more sectors; a sector
/// erased in 1 s; the whole chip in 1 s a sector and 4.2 s of programming it
/// first.
constexpr std::uint64_t BYTE_PROGRAM = 25;
constexpr std::uint64_t ERASE_TIMEOUT = 154;
constexpr std::uint64_t SECTOR_ERASE = CPU_CLOCK_RATE;
constexpr std::uint64_t CHIP_ERASE = ( CPU_CLOCK_RATE * 14 ) + ( CPU_CLOCK_RATE * 42 / 10 );

/// The top boot sectors' starts: six of 64 KB in bank 2, and in bank 1 one
/// of 16 KB, one of 32 KB, four of 8 KB, one of 32 KB and one of 16 KB.
constexpr std::array<std::uint32_t, 15> SECTOR_STARTS{ 0x00000, 0x10000, 0x20000, 0x30000, 0x40000,
                                                       0x50000, 0x60000, 0x64000, 0x6C000, 0x6E000,
                                                       0x70000, 0x72000, 0x74000, 0x7C000, 0x80000 };
constexpr std::uint16_t ALL_SECTORS = ( 1U << 14U ) - 1;
/// Bank 1, the top 128 KB; bank 2 is the six sectors below.
constexpr std::uint32_t BANK_1 = 0x60000;

/// In the byte mode a command's address is $AAA or $555 in A-1 to A10.
constexpr std::uint32_t COMMAND_ADDRESS_MASK = 0xFFF;

unsigned sectorOf( std::uint32_t offset )
{
  auto const next = std::ranges::upper_bound( SECTOR_STARTS, offset );
  return static_cast<unsigned>( next - SECTOR_STARTS.begin() ) - 1;
}

} // namespace

Flash::Flash( System& system, FlashState& state ) : mSystem{ &system }, mState{ &state } {}

std::span<std::uint8_t> Flash::contents() const
{
  return mSystem->getMemory( MemoryType::PRG_ROM );
}

bool Flash::readsArray() const
{
  return mState->mode == FlashMode::READ;
}

void Flash::setMode( FlashMode mode )
{
  bool const wasArray = readsArray();
  mState->mode = mode;
  if ( wasArray != readsArray() )
  {
    mSystem->getMemoryManager()->refreshMappings();
  }
}

void Flash::run()
{
  std::uint64_t const now = mSystem->getMasterClock();
  if ( mState->mode == FlashMode::PROGRAM && now >= mState->end )
  {
    // A 0 cannot be programmed back to a 1: only an erase makes 1s.
    contents()[mState->address] &= mState->data;
    mState->written = true;
    setMode( mState->fast ? FlashMode::FAST : FlashMode::READ );
  }
  else if ( mState->mode == FlashMode::ERASE && now >= mState->end )
  {
    for ( unsigned sector = 0; sector < 14; ++sector )
    {
      if ( ( mState->sectors & ( 1U << sector ) ) != 0 )
      {
        std::span<std::uint8_t> const sectorBytes = contents().subspan(
            SECTOR_STARTS.at( sector ), SECTOR_STARTS.at( sector + 1 ) - SECTOR_STARTS.at( sector ) );
        std::ranges::fill( sectorBytes, std::uint8_t{ 0xFF } );
      }
    }
    mState->sectors = 0;
    mState->written = true;
    setMode( FlashMode::READ );
  }
}

std::uint8_t Flash::read( std::uint32_t offset )
{
  run();
  offset &= SIZE - 1;
  switch ( mState->mode )
  {
  case FlashMode::READ:
    break;
  case FlashMode::AUTOSELECT:
    return autoselect( offset );
  case FlashMode::FAST:
    // Reads are for after Fast Mode, says the datasheet; ares gives the
    // flags of a program done.
    return static_cast<std::uint8_t>( ( contents()[offset] & 0x80U ) | 0x04U );
  case FlashMode::PROGRAM:
  case FlashMode::ERASE:
    if ( busyBank( offset ) )
    {
      return status( offset );
    }
    break;
  }
  return contents()[offset];
}

std::uint8_t Flash::status( std::uint32_t offset )
{
  // Table 9: DQ7 the complement of the bit programmed, or 0 while erasing;
  // DQ6 toggling; DQ3 set once the erase has begun; DQ2 1 while programming,
  // and toggling in a sector being erased.
  mState->toggle = !mState->toggle;
  std::uint8_t flags = mState->toggle ? 0x40U : 0x00U;
  if ( mState->mode == FlashMode::PROGRAM )
  {
    return static_cast<std::uint8_t>( flags | ( ( mState->data & 0x80U ) ^ 0x80U ) | 0x04U );
  }
  if ( mSystem->getMasterClock() >= mState->timeoutEnd )
  {
    flags |= 0x08U;
  }
  if ( ( mState->sectors & ( 1U << sectorOf( offset ) ) ) != 0 )
  {
    mState->eraseToggle = !mState->eraseToggle;
    flags |= mState->eraseToggle ? 0x04U : 0x00U;
  }
  else
  {
    flags |= 0x04U;
  }
  return flags;
}

std::uint8_t Flash::autoselect( std::uint32_t offset )
{
  // The code is picked by A6, A1 and A0, and in the byte mode A-1 picks its
  // byte: the manufacturer's $0004, the device's $220C, and 0 for a sector
  // not protected.
  unsigned const which =
      ( ( ( offset >> 7U ) & 1U ) << 2U ) | ( ( ( offset >> 2U ) & 1U ) << 1U ) | ( ( offset >> 1U ) & 1U );
  std::uint16_t code = 0x0000;
  if ( which == 0 )
  {
    code = 0x0004;
  }
  else if ( which == 1 )
  {
    code = 0x220C;
  }
  return static_cast<std::uint8_t>( ( offset & 1U ) != 0 ? code >> 8U : code );
}

bool Flash::busyBank( std::uint32_t offset ) const
{
  if ( mState->mode == FlashMode::PROGRAM )
  {
    return ( offset >= BANK_1 ) == ( mState->address >= BANK_1 );
  }
  if ( mState->chip )
  {
    return true;
  }
  auto const bank1Sectors = static_cast<std::uint16_t>( ALL_SECTORS & ~0x3FU );
  bool const erasing1 = ( mState->sectors & bank1Sectors ) != 0;
  bool const erasing2 = ( mState->sectors & 0x3FU ) != 0;
  return offset >= BANK_1 ? erasing1 : erasing2;
}

void Flash::write( std::uint32_t offset, std::uint8_t value )
{
  run();
  offset &= SIZE - 1;
  std::uint64_t const now = mSystem->getMasterClock();
  FlashState& state = *mState;

  if ( state.mode == FlashMode::PROGRAM )
  {
    // Any command written while it programs is ignored.
    return;
  }
  if ( state.mode == FlashMode::ERASE )
  {
    // Within the 50 us after a sector's $30, another sector's $30 joins
    // the erase and restarts the wait; anything else but Erase Suspend
    // drops the command. Once it erases, nothing is taken: Erase Suspend is
    // not emulated.
    if ( now < state.timeoutEnd && !state.chip )
    {
      if ( value == 0x30 )
      {
        state.sectors |= static_cast<std::uint16_t>( 1U << sectorOf( offset ) );
        state.timeoutEnd = now + ERASE_TIMEOUT;
        state.end = state.timeoutEnd + ( SECTOR_ERASE * static_cast<unsigned>( std::popcount( state.sectors ) ) );
      }
      else if ( value != 0xB0 )
      {
        state.sectors = 0;
        setMode( FlashMode::READ );
      }
    }
    return;
  }
  if ( state.mode == FlashMode::FAST )
  {
    if ( state.programSetup )
    {
      state.programSetup = false;
      startProgram( offset, value );
    }
    else if ( state.fastResetSetup && ( value == 0xF0 || value == 0x00 ) )
    {
      state.fastResetSetup = false;
      state.fast = false;
      setMode( FlashMode::READ );
    }
    else
    {
      state.programSetup = value == 0xA0;
      state.fastResetSetup = value == 0x90;
    }
    return;
  }

  // Read or autoselect: the command sequences of table 8.
  if ( state.programSetup )
  {
    state.programSetup = false;
    startProgram( offset, value );
    return;
  }
  std::uint32_t const at = offset & COMMAND_ADDRESS_MASK;
  switch ( state.unlock )
  {
  case 0:
    if ( value == 0xF0 )
    {
      state.eraseSetup = false;
      setMode( FlashMode::READ );
    }
    else if ( at == 0xAAA && value == 0xAA )
    {
      state.unlock = 1;
    }
    else
    {
      state.eraseSetup = false;
    }
    return;
  case 1:
    state.unlock = at == 0x555 && value == 0x55 ? 2 : 0;
    if ( state.unlock == 0 )
    {
      state.eraseSetup = false;
    }
    return;
  default:
    break;
  }

  state.unlock = 0;
  if ( state.eraseSetup )
  {
    state.eraseSetup = false;
    if ( value == 0x10 && at == 0xAAA )
    {
      startChipErase();
    }
    else if ( value == 0x30 )
    {
      startSectorErase( offset );
    }
    return;
  }
  if ( at != 0xAAA )
  {
    return;
  }
  switch ( value )
  {
  case 0xF0:
    setMode( FlashMode::READ );
    break;
  case 0x90:
    setMode( FlashMode::AUTOSELECT );
    break;
  case 0xA0:
    state.programSetup = true;
    break;
  case 0x80:
    state.eraseSetup = true;
    break;
  case 0x20:
    state.fast = true;
    state.programSetup = false;
    state.fastResetSetup = false;
    setMode( FlashMode::FAST );
    break;
  default:
    break;
  }
}

void Flash::startProgram( std::uint32_t offset, std::uint8_t value )
{
  mState->address = offset;
  mState->data = value;
  mState->end = mSystem->getMasterClock() + BYTE_PROGRAM;
  setMode( FlashMode::PROGRAM );
}

void Flash::startSectorErase( std::uint32_t offset )
{
  mState->chip = false;
  mState->sectors = static_cast<std::uint16_t>( 1U << sectorOf( offset ) );
  mState->timeoutEnd = mSystem->getMasterClock() + ERASE_TIMEOUT;
  mState->end = mState->timeoutEnd + SECTOR_ERASE;
  setMode( FlashMode::ERASE );
}

void Flash::startChipErase()
{
  mState->chip = true;
  mState->sectors = ALL_SECTORS;
  mState->timeoutEnd = mSystem->getMasterClock();
  mState->end = mState->timeoutEnd + CHIP_ERASE;
  setMode( FlashMode::ERASE );
}

} // namespace ws::machine
