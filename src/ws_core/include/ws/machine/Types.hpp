#pragma once

// The state of every part of the WonderSwan, as the parts keep it and as the
// protocol and the debugger read it. Ported from Mesen2's Core/WS/WsTypes.h at
// b9fa69ddc6d0a331fb103fdb5eef6904305703c2; a name keeps Mesen2's words
// (docs/decisions/0002-references.md). Every struct here is trivially copyable,
// so that a part's state goes into a save state whole.

#include <array>
#include <cstdint>

namespace ws::machine
{

/// The three machines: the WonderSwan, the WonderSwan Color, and the
/// SwanCrystal, a Color with another LCD.
enum class Model : std::uint8_t
{
  MONOCHROME,
  COLOR,
  SWAN_CRYSTAL
};

/// Whether the sound goes to the speaker or the headphones, which the
/// hardware tells apart by the plug.
enum class AudioMode : std::uint8_t
{
  HEADPHONES,
  SPEAKERS
};

/// The buttons, as bits of a mask, in Mesen2's WsController order. The
/// hardware names the two pads X and Y: UP, RIGHT, DOWN and LEFT are X1 to
/// X4, and UP2, RIGHT2, DOWN2 and LEFT2 are Y1 to Y4.
enum class Button : std::uint8_t
{
  UP,
  DOWN,
  LEFT,
  RIGHT,
  UP2,
  DOWN2,
  LEFT2,
  RIGHT2,
  SOUND,
  START,
  B,
  A
};

inline constexpr unsigned BUTTON_COUNT = 12;

/// The LCD as the machine draws it, and the clock of everything: the CPU's
/// 3.072 MHz, which the PPU also steps at, a dot a cycle.
inline constexpr std::uint32_t SCREEN_WIDTH = 224;
inline constexpr std::uint32_t SCREEN_HEIGHT = 144;
inline constexpr std::uint32_t CLOCKS_PER_SCANLINE = 256;
inline constexpr std::uint32_t SCANLINE_COUNT = 159;
inline constexpr std::uint32_t PIXEL_COUNT = SCREEN_WIDTH * SCREEN_HEIGHT;
/// The picture with the LCD's icons drawn beside or below it.
inline constexpr std::uint32_t MAX_PIXEL_COUNT = SCREEN_WIDTH * ( SCREEN_HEIGHT + 13 );
inline constexpr std::uint32_t CPU_CLOCK_RATE = 12'288'000 / 4;

struct CpuFlags
{
  bool carry{};     // 0x01
  bool parity{};    // 0x04
  bool auxCarry{};  // 0x10
  bool zero{};      // 0x40
  bool sign{};      // 0x80
  bool trap{};      // 0x100
  bool irq{};       // 0x200
  bool direction{}; // 0x400
  bool overflow{};  // 0x800
  bool mode{};      // 0x8000

  [[nodiscard]] std::uint16_t get() const
  {
    return static_cast<std::uint16_t>(
        static_cast<unsigned>( carry ) | ( static_cast<unsigned>( parity ) << 2U ) |
        ( static_cast<unsigned>( auxCarry ) << 4U ) | ( static_cast<unsigned>( zero ) << 6U ) |
        ( static_cast<unsigned>( sign ) << 7U ) | ( static_cast<unsigned>( trap ) << 8U ) |
        ( static_cast<unsigned>( irq ) << 9U ) | ( static_cast<unsigned>( direction ) << 10U ) |
        ( static_cast<unsigned>( overflow ) << 11U ) | ( static_cast<unsigned>( mode ) << 15U ) | 0x7002U );
  }

  void set( std::uint16_t f )
  {
    carry = ( f & 0x01U ) != 0;
    parity = ( f & 0x04U ) != 0;
    auxCarry = ( f & 0x10U ) != 0;
    zero = ( f & 0x40U ) != 0;
    sign = ( f & 0x80U ) != 0;
    trap = ( f & 0x100U ) != 0;
    irq = ( f & 0x200U ) != 0;
    direction = ( f & 0x400U ) != 0;
    overflow = ( f & 0x800U ) != 0;
    mode = ( f & 0x8000U ) != 0;
  }
};

/// The V30MZ's registers, and the cycles it has run since power-up.
struct CpuState
{
  std::uint64_t cycleCount{};

  std::uint16_t cs{};
  std::uint16_t ip{};

  std::uint16_t ss{};
  std::uint16_t sp{};
  std::uint16_t bp{};

  std::uint16_t ds{};
  std::uint16_t es{};

  std::uint16_t si{};
  std::uint16_t di{};

  std::uint16_t ax{};
  std::uint16_t bx{};
  std::uint16_t cx{};
  std::uint16_t dx{};

  CpuFlags flags{};
  bool halted{};
  bool powerOff{};
};

struct BgLayer
{
  std::uint16_t mapAddress{};
  std::uint16_t mapAddressLatch{};

  std::uint8_t scrollX{};
  std::uint8_t scrollXLatch{};

  std::uint8_t scrollY{};
  std::uint8_t scrollYLatch{};

  bool enabled{};
  bool enabledLatch{};

  void latch()
  {
    enabledLatch = enabled;
    scrollXLatch = scrollX;
    scrollYLatch = scrollY;
    mapAddressLatch = mapAddress;
  }
};

struct Window
{
  bool enabled{};
  bool enabledLatch{};

  std::uint8_t left{};
  std::uint8_t leftLatch{};
  std::uint8_t right{};
  std::uint8_t rightLatch{};
  std::uint8_t top{};
  std::uint8_t topLatch{};
  std::uint8_t bottom{};
  std::uint8_t bottomLatch{};

  [[nodiscard]] bool isInsideWindow( std::uint8_t x, std::uint8_t y ) const
  {
    return x >= leftLatch && x <= rightLatch && y >= topLatch && y <= bottomLatch;
  }

  void latch()
  {
    enabledLatch = enabled;
    leftLatch = left;
    rightLatch = right;
    topLatch = top;
    bottomLatch = bottom;
  }
};

struct LcdIcons
{
  bool sleep{};
  bool vertical{};
  bool horizontal{};
  bool aux1{};
  bool aux2{};
  bool aux3{};

  std::uint8_t value{};
};

enum class VideoMode : std::uint8_t
{
  MONOCHROME,
  COLOR_2BPP,
  COLOR_4BPP,
  COLOR_4BPP_PACKED
};

struct PpuState
{
  std::uint32_t frameCount{};
  std::uint16_t cycle{};
  std::uint16_t scanline{};

  std::array<BgLayer, 2> bgLayers{};
  Window bgWindow{};
  Window spriteWindow{};
  bool drawOutsideBgWindow{};
  bool drawOutsideBgWindowLatch{};

  std::array<std::uint8_t, 0x40> bwPalettes{};
  std::array<std::uint8_t, 8> bwShades{};

  std::uint16_t spriteTableAddress{};
  std::uint8_t firstSpriteIndex{};
  std::uint8_t spriteCount{};
  std::uint8_t spriteCountLatch{};
  bool spritesEnabled{};
  bool spritesEnabledLatch{};

  VideoMode mode{};
  VideoMode nextMode{};

  std::uint8_t bgColor{};
  std::uint8_t irqScanline{};

  bool lcdEnabled{};
  bool highContrast{};
  bool sleepEnabled{};

  std::uint8_t lcdControl{};

  LcdIcons icons{};

  std::uint8_t lastScanline{};
  std::uint8_t backPorchScanline{};

  std::uint32_t showVolumeIconFrame{};
  std::array<std::uint8_t, 8> lcdTftConfig{};

  std::uint8_t control{};       // $00
  std::uint8_t screenAddress{}; // $07
};

enum class Segment : std::uint8_t
{
  DEFAULT,
  ES,
  SS,
  CS,
  DS
};

enum class IrqSource : std::uint8_t
{
  UART_SEND_READY = 0x01,
  KEY_PRESSED = 0x02,
  CART = 0x04,
  UART_RECV_READY = 0x08,
  SCANLINE = 0x10,
  VERTICAL_BLANK_TIMER = 0x20,
  VERTICAL_BLANK = 0x40,
  HORIZONTAL_BLANK_TIMER = 0x80
};

struct MemoryManagerState
{
  std::uint8_t activeIrqs{};
  std::uint8_t enabledIrqs{};
  std::uint8_t irqVectorOffset{};

  std::uint8_t systemControl2{};
  std::uint8_t systemTest{};

  bool colorEnabled{};
  bool enable4bpp{};
  bool enable4bppPacked{};

  bool bootRomDisabled{};
  bool cartWordBus{};
  bool slowRom{};

  bool slowSram{};
  bool slowPort{};

  bool enableLowBatteryNmi{};
  bool powerOffRequested{};
};

struct ControlManagerState
{
  std::uint8_t inputSelect{};
};

struct DmaControllerState
{
  std::uint32_t gdmaSrc{};
  std::uint32_t sdmaSrc{};
  std::uint32_t sdmaLength{};
  std::uint32_t sdmaSrcReloadValue{};
  std::uint32_t sdmaLengthReloadValue{};

  std::uint16_t gdmaDest{};
  std::uint16_t gdmaLength{};
  std::uint8_t gdmaControl{};
  std::uint8_t sdmaControl{};

  bool sdmaEnabled{};
  bool sdmaDecrement{};
  bool sdmaHyperVoice{};
  bool sdmaRepeat{};
  bool sdmaHold{};
  std::uint8_t sdmaFrequency{};
  std::uint8_t sdmaTimer{};
};

struct TimerState
{
  std::uint16_t hTimer{};
  std::uint16_t vTimer{};

  std::uint16_t hReloadValue{};
  std::uint16_t vReloadValue{};

  std::uint8_t control{};
  bool hBlankEnabled{};
  bool hBlankAutoReload{};
  bool vBlankEnabled{};
  bool vBlankAutoReload{};
};

struct BaseApuState
{
  std::uint16_t frequency{};
  std::uint16_t timer{};

  bool enabled{};
  std::uint8_t leftVolume{};
  std::uint8_t rightVolume{};

  std::uint8_t samplePosition{};
  std::uint8_t leftOutput{};
  std::uint8_t rightOutput{};

  void setVolume( std::uint8_t value )
  {
    rightVolume = value & 0x0FU;
    leftVolume = static_cast<std::uint8_t>( value >> 4U );
  }

  [[nodiscard]] std::uint8_t getVolume() const
  {
    return static_cast<std::uint8_t>( rightVolume | ( leftVolume << 4U ) );
  }
};

struct ApuCh1State : BaseApuState
{
};

struct ApuCh2State : BaseApuState
{
  bool pcmEnabled{};
  bool maxPcmVolumeRight{};
  bool halfPcmVolumeRight{};
  bool maxPcmVolumeLeft{};
  bool halfPcmVolumeLeft{};
};

struct ApuCh3State : BaseApuState
{
  std::uint16_t sweepScaler{};
  bool sweepEnabled{};
  std::int8_t sweepValue{};
  std::uint8_t sweepPeriod{};
  std::uint8_t sweepTimer{};
  bool useSweepCpuClock{};
};

struct ApuCh4State : BaseApuState
{
  bool noiseEnabled{};
  bool lfsrEnabled{};
  std::uint8_t tapMode{};
  std::uint8_t tapShift{};
  std::uint16_t lfsr{};
  std::uint8_t holdLfsr{};
};

enum class HyperVoiceScalingMode : std::uint8_t
{
  UNSIGNED,
  UNSIGNED_NEGATED,
  SIGNED,
  NONE
};

enum class HyperVoiceChannelMode : std::uint8_t
{
  STEREO,
  MONO_LEFT,
  MONO_RIGHT,
  MONO_BOTH
};

struct ApuHyperVoiceState
{
  std::int16_t leftOutput{};
  std::int16_t rightOutput{};

  bool enabled{};

  std::uint8_t leftSample{};
  std::uint8_t rightSample{};
  bool updateRightValue{};

  std::uint8_t divisor{};
  std::uint8_t timer{};
  std::uint8_t input{};
  std::uint8_t shift{};
  HyperVoiceChannelMode channelMode{};
  HyperVoiceScalingMode scalingMode{};

  std::uint8_t controlLow{};
  std::uint8_t controlHigh{};
};

struct ApuState
{
  ApuCh1State ch1{};
  ApuCh2State ch2{};
  ApuCh3State ch3{};
  ApuCh4State ch4{};
  ApuHyperVoiceState voice{};

  std::uint16_t waveTableAddress{};
  bool speakerEnabled{};
  std::uint8_t speakerVolume{};
  std::uint8_t internalMasterVolume{};
  std::uint8_t masterVolume{};
  bool headphoneEnabled{};

  bool holdChannels{};
  bool forceOutput2{};
  bool forceOutput4{};
  bool forceOutputCh2Voice{};

  std::uint8_t soundTest{};
};

struct SerialState
{
  std::uint64_t sendClock{};

  bool enabled{};
  bool highSpeed{};
  bool receiveOverflow{};

  bool hasReceiveData{};
  std::uint8_t receiveBuffer{};

  bool hasSendData{};
  std::uint8_t sendBuffer{};
};

enum class EepromSize : std::uint16_t
{
  SIZE_0 = 0,
  SIZE_128 = 0x80,
  SIZE_1KB = 0x400,
  SIZE_2KB = 0x800
};

struct EepromState
{
  std::uint64_t cmdStartClock{};
  EepromSize size{};
  std::uint16_t readBuffer{};
  std::uint16_t writeBuffer{};
  std::uint16_t command{};
  std::uint16_t control{};
  bool writeDisabled{};
  bool readDone{};
  bool idle{};

  bool internalEepromWriteProtected{};
};

/// Where a command of the Bandai 2003's RTC interface stands.
enum class RtcPhase : std::uint8_t
{
  /// No command runs.
  IDLE,
  /// The command byte goes out to the S-3511A.
  COMMAND,
  /// A payload byte goes out, or comes in.
  PAYLOAD,
  /// The 2003 waits for the next payload byte to be written to $CB.
  WAIT_WRITE,
  /// The 2003 waits for the payload byte it holds to be read from $CB.
  WAIT_READ
};

/// The Bandai 2003's RTC interface and the Seiko S-3511A behind it.
struct RtcState
{
  /// The S-3511A's date and time, BCD: year, month, day, day of the week,
  /// hour (bit 7 PM), minute, second.
  std::array<std::uint8_t, 7> dateTime{};
  /// Its configuration: power failure, 24-hour mode, the interrupt mode.
  std::uint8_t config{};
  /// Its alarm, or the frequency of its interrupt.
  std::array<std::uint8_t, 2> alarm{};
  /// Cycles of the CPU's clock into the current second, and the master clock
  /// the time was last brought up to.
  std::uint32_t subsecond{};
  std::uint64_t lastClock{};
  /// Whether the per-minute interrupt has been raised and not yet taken.
  bool minuteIrq{};

  /// $CA as the 2003 holds it: the command written, bit 4 while it runs.
  std::uint8_t command{};
  bool ready{};
  RtcPhase phase{ RtcPhase::IDLE };
  /// The master clock at which the byte going out or coming in is through.
  std::uint64_t phaseEnd{};
  /// $CB: the payload byte held, the next to send or the last received.
  std::uint8_t data{};
  /// The command's payload, and the bytes of it through.
  std::array<std::uint8_t, 7> payload{};
  std::uint8_t index{};
};

struct CartState
{
  /// $C0-$C3, and on the 2003 the high bits $CF-$D5 add: the 1 MB bank at
  /// $40000, and the 64 KB banks at $10000, $20000 and $30000.
  std::array<std::uint16_t, 4> selectedBanks{};
  /// The 2003's general-purpose outputs, $CC and $CD.
  std::uint8_t gpoEnable{};
  std::uint8_t gpoData{};
  /// The 2003's $CE: the ROM, not the SRAM, at $10000-$1FFFF.
  bool flashSelected{};
  RtcState rtc{};
};

/// Every part's state at once, as the debugger shows it.
struct State
{
  CpuState cpu{};
  PpuState ppu{};
  ApuState apu{};
  MemoryManagerState memoryManager{};
  ControlManagerState controlManager{};
  DmaControllerState dmaController{};
  TimerState timer{};
  SerialState serial{};
  EepromState internalEeprom{};
  CartState cart{};
  EepromState cartEeprom{};
  Model model{};
};

} // namespace ws::machine
