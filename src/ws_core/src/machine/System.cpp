#include "System.hpp"

#include "Apu.hpp"
#include "Cart.hpp"
#include "ControlManager.hpp"
#include "DmaController.hpp"
#include "Eeprom.hpp"
#include "MemoryManager.hpp"
#include "Ppu.hpp"
#include "Serial.hpp"
#include "StateArchive.hpp"
#include "Timer.hpp"

#include "cpu/Cpu.hpp"

#include <algorithm>
#include <bit>
#include <utility>

namespace ws::machine
{

RomInfo readRomInfo( std::span<std::uint8_t const> rom )
{
  RomInfo info;
  info.romSize = std::bit_ceil( rom.size() );
  std::size_t const end = rom.size();

  switch ( rom[end - 5] )
  {
  case 0x00:
    break;
  case 0x01:
  case 0x02:
    info.saveRamSize = 32 * 1024;
    break;
  case 0x03:
    info.saveRamSize = 128 * 1024;
    break;
  case 0x04:
    info.saveRamSize = 256 * 1024;
    break;
  case 0x05:
    info.saveRamSize = 512 * 1024;
    break;
  case 0x10:
    info.cartEepromSize = 0x80;
    break;
  case 0x20:
    info.cartEepromSize = 0x800;
    break;
  case 0x50:
    info.cartEepromSize = 0x400;
    break;
  default:
    break;
  }

  info.publisher = rom[end - 10];
  info.colorSupport = ( rom[end - 9] & 0x01U ) != 0;
  info.gameId = rom[end - 8];
  info.vertical = ( rom[end - 4] & 0x01U ) != 0;
  info.mapper = rom[end - 3];
  info.checksum = static_cast<std::uint16_t>( rom[end - 2] | ( rom[end - 1] << 8U ) );
  return info;
}

System::System( Config const& config,
                std::vector<std::uint8_t> rom,
                std::vector<std::uint8_t> bootRom,
                std::vector<std::uint8_t> internalEeprom,
                SystemListener& listener )
    : mConfig{ config }, mListener{ &listener }, mBootRom{ std::move( bootRom ) }, mPrgRom{ std::move( rom ) }
{
  RomInfo const info = readRomInfo( mPrgRom );
  if ( mPrgRom.size() < info.romSize )
  {
    // If size isn't a power of 2, pad the beginning of the ROM to the next power of 2
    mPrgRom.insert( mPrgRom.begin(), info.romSize - mPrgRom.size(), 0 );
  }

  mSaveRam.assign( info.saveRamSize, 0 );
  mCartEepromData.assign( info.cartEepromSize, 0 );
  mWorkRam.assign( mConfig.model == Model::MONOCHROME ? 0x4000 : 0x10000, 0 );

  auto const internalEepromSize =
      static_cast<std::size_t>( mConfig.model == Model::MONOCHROME ? EepromSize::SIZE_128 : EepromSize::SIZE_2KB );
  if ( internalEeprom.size() == internalEepromSize )
  {
    mInternalEepromData = std::move( internalEeprom );
  }
  else
  {
    mInternalEepromData.assign( internalEepromSize, 0 );
    Eeprom::initInternalEepromData( mConfig.model, mInternalEepromData );
  }

  bool const hasBootRom = !mBootRom.empty();
  mInternalEeprom = std::make_unique<Eeprom>( *this, mInternalEepromData, true, hasBootRom );
  if ( !mCartEepromData.empty() )
  {
    mCartEeprom = std::make_unique<Eeprom>( *this, mCartEepromData, false, hasBootRom );
  }

  mControlManager = std::make_unique<ControlManager>( *this );
  mMemoryManager = std::make_unique<MemoryManager>( *this );
  mCpu = std::make_unique<Cpu>( *this );
  mTimer = std::make_unique<Timer>( *this );
  mSerial = std::make_unique<Serial>( *this );
  mDmaController = std::make_unique<DmaController>( *this );
  mPpu = std::make_unique<Ppu>( *this );
  mApu = std::make_unique<Apu>( *this );
  mCart = std::make_unique<Cart>( *this );

  mMemoryManager->init();

  if ( !hasBootRom )
  {
    initPostBootRomState();
  }
}

System::~System() = default;

void System::exec()
{
  mCpu->exec();
}

void System::initPostBootRomState()
{
  // Init work ram
  if ( mConfig.model == Model::MONOCHROME )
  {
    std::ranges::fill( mWorkRam, std::uint8_t{} );
  }
  else
  {
    std::fill( mWorkRam.begin(), mWorkRam.begin() + 0xFE00, std::uint8_t{} );
    std::fill( mWorkRam.begin() + 0xFE00, mWorkRam.end(), std::uint8_t{ 0xFF } );
  }

  // Init port state
  CpuState& cpu = mCpu->getState();
  cpu.ds = 0xFE00;
  cpu.cs = 0xFFFF;
  cpu.sp = 0x2000;

  if ( mConfig.model == Model::MONOCHROME )
  {
    cpu.ax = 0xFF85;
    cpu.bx = 0x0040;
    cpu.dx = 0x0005;
    cpu.si = 0x023D;
    cpu.di = 0x040D;
    cpu.flags.sign = true;
  }
  else
  {
    cpu.ax = 0xFF87;
    cpu.bx = 0x0043;
    cpu.dx = 0x0001;
    cpu.si = 0x0435;
    cpu.di = 0x040B;
    cpu.flags.sign = true;
    cpu.flags.parity = true;
  }

  PpuState& ppu = mPpu->getState();
  ppu.lcdEnabled = true;
  if ( mConfig.model == Model::MONOCHROME )
  {
    ppu.scanline = 26;
    ppu.cycle = 53;

    ppu.bwPalettes = { 0, 0, 0, 7, 0, 0, 0, 5, 0, 0, 0, 4, 0, 0, 0, 3, 0, 0, 0, 2, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0,
                       0, 3, 5, 7, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    ppu.bwShades = { 0, 2, 4, 6, 8, 10, 12, 15 };
  }
  else
  {
    ppu.scanline = 127;
    ppu.cycle = 232;
  }

  mPpu->setOutputToBgColor();

  MemoryManagerState& mm = mMemoryManager->getState();
  mm.bootRomDisabled = true;
  mm.cartWordBus = true;
  if ( mConfig.model != Model::MONOCHROME )
  {
    mm.slowSram = true;
    mm.slowPort = true;
  }

  ApuState& apu = mApu->getState();
  if ( mConfig.model == Model::MONOCHROME )
  {
    apu.ch1.frequency = 0x7E6;
    apu.ch2.frequency = 0x7E6;
  }
  else
  {
    apu.internalMasterVolume = mInternalEepromData[0x83] & 0x03U;
    ppu.highContrast = ( mInternalEepromData[0x83] & 0x40U ) != 0;
  }

  EepromState& eeprom = mInternalEeprom->getState();
  eeprom.readDone = true;
  eeprom.idle = true;
  eeprom.internalEepromWriteProtected = true;

  // Copy data from rom header to internal eeprom, like the boot rom would
  std::size_t const header = mPrgRom.size() - 0x10;
  bool const supportsColor = ( mPrgRom[header + 7] & 0x01U ) != 0;
  for ( std::size_t i = 0; i < 3; i++ )
  {
    mInternalEepromData[0x76 + i] = mPrgRom[header + 6 + i];

    if ( mConfig.model != Model::MONOCHROME && supportsColor )
    {
      // For games that support color & color models, copy 76-78 to 80-82
      mInternalEepromData[0x80 + i] = mInternalEepromData[0x76 + i];
    }
  }

  mMemoryManager->refreshMappings();
}

Model System::getModel() const
{
  return mConfig.model;
}

Config const& System::getConfig() const
{
  return mConfig;
}

bool System::isColorMode() const
{
  return mMemoryManager->isColorEnabled();
}

bool System::isPowerOff() const
{
  return mCpu->isPowerOff();
}

bool System::isVerticalMode() const
{
  return mVerticalMode;
}

std::uint64_t System::getMasterClock() const
{
  return mCpu->getCycleCount();
}

Cpu* System::getCpu() const
{
  return mCpu.get();
}

Ppu* System::getPpu() const
{
  return mPpu.get();
}

Apu* System::getApu() const
{
  return mApu.get();
}

MemoryManager* System::getMemoryManager() const
{
  return mMemoryManager.get();
}

ControlManager* System::getControlManager() const
{
  return mControlManager.get();
}

DmaController* System::getDmaController() const
{
  return mDmaController.get();
}

Timer* System::getTimer() const
{
  return mTimer.get();
}

Serial* System::getSerial() const
{
  return mSerial.get();
}

Cart* System::getCart() const
{
  return mCart.get();
}

Eeprom* System::getInternalEeprom() const
{
  return mInternalEeprom.get();
}

Eeprom* System::getCartEeprom() const
{
  return mCartEeprom.get();
}

std::span<std::uint8_t> System::getMemory( MemoryType type )
{
  switch ( type )
  {
  case MemoryType::WORK_RAM:
    return mWorkRam;
  case MemoryType::PRG_ROM:
    return mPrgRom;
  case MemoryType::CART_RAM:
    return mSaveRam;
  case MemoryType::BOOT_ROM:
    return mBootRom;
  case MemoryType::INTERNAL_EEPROM:
    return mInternalEepromData;
  case MemoryType::CART_EEPROM:
    return mCartEepromData;
  case MemoryType::NONE:
    break;
  }
  return {};
}

std::span<std::uint8_t const> System::getMemory( MemoryType type ) const
{
  return const_cast<System*>( this )->getMemory( type );
}

State System::getState() const
{
  State state;
  state.model = mConfig.model;
  state.cpu = mCpu->getState();
  state.ppu = mPpu->getState();
  state.apu = mApu->getState();
  state.memoryManager = mMemoryManager->getState();
  state.controlManager = mControlManager->getState();
  state.dmaController = mDmaController->getState();
  state.timer = mTimer->getState();
  state.serial = mSerial->getState();
  state.internalEeprom = mInternalEeprom->getState();
  state.cart = mCart->getState();
  if ( mCartEeprom )
  {
    state.cartEeprom = mCartEeprom->getState();
  }
  return state;
}

void System::setButtons( std::uint16_t buttons )
{
  mControlManager->setButtons( buttons );
}

std::uint16_t System::getButtons() const
{
  return mControlManager->getButtons();
}

void System::frameDone( std::span<std::uint16_t const> pixels, std::uint32_t width, std::uint32_t height )
{
  mListener->frameDone( pixels, width, height );
  mControlManager->updateInputState();

  // Mesen2's RunFrame, after the frame: the EEPROMs' writes made visible,
  // and the orientation the LCD's icons ask for.
  mApu->playQueuedAudio();
  mInternalEeprom->run();
  if ( mCartEeprom )
  {
    mCartEeprom->run();
  }

  LcdIcons const& icons = mPpu->getState().icons;
  mVerticalMode |= icons.vertical;
  if ( icons.horizontal )
  {
    mVerticalMode = false;
  }
}

void System::audioDone( std::span<std::int16_t const> samples )
{
  mListener->audioDone( samples );
}

void System::setBusObserver( BusObserver* observer )
{
  mObserver = observer;
}

template <class Archive>
void System::serialize( Archive& archive )
{
  archive.chunk( fourCc( "CPU " ), 1, *mCpu );
  archive.chunk( fourCc( "PPU " ), 1, *mPpu );
  archive.chunk( fourCc( "APU " ), 1, *mApu );
  // Process cart before memory manager to ensure mappings are updated properly
  archive.chunk( fourCc( "CART" ), 1, *mCart );
  archive.chunk( fourCc( "MEMM" ), 1, *mMemoryManager );
  archive.chunk( fourCc( "DMA " ), 1, *mDmaController );
  archive.chunk( fourCc( "CTRL" ), 1, *mControlManager );
  archive.chunk( fourCc( "TIMR" ), 1, *mTimer );
  archive.chunk( fourCc( "SERL" ), 1, *mSerial );
  archive.chunk( fourCc( "IEEP" ), 1, *mInternalEeprom );
  if ( mCartEeprom )
  {
    archive.chunk( fourCc( "CEEP" ), 1, *mCartEeprom );
  }

  // The memories keep their sizes: a vector read in at another is refused.
  auto const keep = [&archive]( std::vector<std::uint8_t>& memory )
  {
    std::size_t const size = memory.size();
    archive( memory );
    if constexpr ( Archive::READING )
    {
      if ( memory.size() != size )
      {
        memory.resize( size );
        archive.fail();
      }
    }
  };
  archive( mVerticalMode );
  keep( mWorkRam );
  keep( mSaveRam );
  keep( mInternalEepromData );
  keep( mCartEepromData );
}

template void System::serialize( StateWriter& archive );
template void System::serialize( StateReader& archive );

} // namespace ws::machine
