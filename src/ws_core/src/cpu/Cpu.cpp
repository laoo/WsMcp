// The NEC V30MZ. Ported from Mesen2's Core/WS/WsCpu.h and WsCpu.cpp at
// b9fa69ddc6d0a331fb103fdb5eef6904305703c2.

#include "cpu/Cpu.hpp"

#include "machine/MemoryManager.hpp"

#include <bit>
#include <type_traits>
#include <utility>

namespace ws::machine
{

namespace
{

template <typename T>
constexpr bool IS_WORD = std::is_same_v<T, std::uint16_t>;

template <typename T>
constexpr std::uint32_t getBitCount()
{
  return sizeof( T ) * 8;
}

template <typename T>
constexpr std::uint32_t getMaxValue()
{
  return ( 1U << getBitCount<T>() ) - 1;
}

template <typename T>
constexpr std::uint32_t getSign()
{
  return 1U << ( getBitCount<T>() - 1 );
}

bool checkParity( std::uint8_t value )
{
  return ( std::popcount( value ) & 0x01 ) == 0;
}

template <typename T>
bool checkParity( T value )
  requires( !std::is_same_v<T, std::uint8_t> )
{
  return checkParity( static_cast<std::uint8_t>( value ) );
}

} // namespace

Cpu::Cpu( System& system ) : mSystem{ &system }, mPrefetch{ *this, system }
{
  mModRegLut8 = { &mState.ax, &mState.cx, &mState.dx, &mState.bx };
  mModSegLut16 = { &mState.es, &mState.cs, &mState.ss, &mState.ds };
  mModRegLut16 = { &mState.ax, &mState.cx, &mState.dx, &mState.bx, &mState.sp, &mState.bp, &mState.si, &mState.di };

  mState.cs = 0xFFFF;
  mState.ip = 0x0000;

  mState.bx = 0x1C00;
  mState.cx = 0x0004;
  mState.sp = 0x2000;
  mState.di = 0x6FFF;

  mState.flags.mode = true;

  clearPrefetch();
}

MemoryManager& Cpu::mm()
{
  if ( mMemoryManager == nullptr )
  {
    mMemoryManager = mSystem->getMemoryManager();
  }
  return *mMemoryManager;
}

std::uint32_t Cpu::getProgramCounter( bool adjustForRepLoop ) const
{
  std::uint16_t ip = mState.ip;
  if ( adjustForRepLoop && isRepeating() )
  {
    ip = static_cast<std::uint16_t>( ip - mPrefix.prefixCount );
  }
  return ( ( static_cast<std::uint32_t>( mState.cs ) << 4U ) + ip ) & 0xFFFFFU;
}

bool Cpu::isRepeating() const
{
  return mPrefix.preserve && mPrefix.rep != RepMode::NONE;
}

bool Cpu::prepareInstruction()
{
  bool const irqPending = mm().hasPendingIrq();
  if ( mState.halted && ( !irqPending || mState.powerOff ) )
  {
    idle();
    return false;
  }

  if ( irqPending )
  {
    mState.halted = false;
    if ( mState.flags.irq && mSuppressIrqClock != mState.cycleCount )
    {
      interrupt( mm().getIrqVector(), true );
    }
  }

  return true;
}

template <typename T>
T Cpu::readMemory( std::uint16_t seg, std::uint16_t offset )
{
  return mm().read<T>( seg, offset );
}

template <typename T>
void Cpu::writeMemory( std::uint16_t seg, std::uint16_t offset, T value )
{
  mm().write<T>( seg, offset, value );
}

template <typename T>
T Cpu::readPort( std::uint16_t port )
{
  return mm().readPort<T>( port );
}

template <typename T>
void Cpu::writePort( std::uint16_t port, T value )
{
  mm().writePort<T>( port, value );
}

void Cpu::processCpuCycle()
{
  mm().exec();
}

std::uint8_t Cpu::readCodeByte()
{
  std::uint8_t const value = mPrefetch.read();
  mState.ip++;
  return value;
}

std::uint16_t Cpu::readCodeWord()
{
  std::uint8_t const lo = readCodeByte();
  std::uint8_t const hi = readCodeByte();
  return static_cast<std::uint16_t>( ( hi << 8U ) | lo );
}

template <typename T>
T Cpu::readImmediate()
{
  if constexpr ( IS_WORD<T> )
  {
    return readCodeWord();
  }
  else
  {
    return readCodeByte();
  }
}

void Cpu::idle( std::uint8_t cycles )
{
  for ( std::uint8_t i = 0; i < cycles; i++ )
  {
    mPrefetch.prefetch();
  }
}

void Cpu::move( std::uint16_t& dst, std::uint16_t src )
{
  idle();
  dst = src;
}

void Cpu::moveLo( std::uint16_t& dst, std::uint8_t src )
{
  idle();
  dst = static_cast<std::uint16_t>( ( dst & 0xFF00U ) | src );
}

void Cpu::moveHi( std::uint16_t& dst, std::uint8_t src )
{
  idle();
  dst = static_cast<std::uint16_t>( ( dst & 0xFFU ) | ( static_cast<std::uint32_t>( src ) << 8U ) );
}

void Cpu::push( std::uint16_t value )
{
  mState.sp = static_cast<std::uint16_t>( mState.sp - 2 );
  writeMemory<std::uint16_t>( mState.ss, mState.sp, value );
}

void Cpu::pushSp()
{
  mState.sp = static_cast<std::uint16_t>( mState.sp - 2 );
  writeMemory<std::uint16_t>( mState.ss, mState.sp, mState.sp );
}

void Cpu::pop( std::uint16_t& dst )
{
  auto const value = readMemory<std::uint16_t>( mState.ss, mState.sp );
  mState.sp = static_cast<std::uint16_t>( mState.sp + 2 );
  dst = value;
}

void Cpu::pushSegment( std::uint16_t value )
{
  idle( 1 );
  push( value );
}

void Cpu::popSegment( std::uint16_t& dst )
{
  idle( 2 );
  pop( dst );
}

void Cpu::pushFlags()
{
  idle();
  push( mState.flags.get() );
}

void Cpu::popFlags()
{
  idle( 2 );
  std::uint16_t flags = 0;
  pop( flags );
  setFlags( flags );
}

void Cpu::setFlags( std::uint16_t flags )
{
  bool const irq = mState.flags.irq;
  bool const trace = mState.flags.trap;
  mState.flags.set( flags );
  bool const trapEnabled = !trace && mState.flags.trap;
  if ( ( !irq && mState.flags.irq ) || trapEnabled )
  {
    // Suppress IRQs for the next instruction when irq flag gets set
    suppressIrq( trapEnabled );
  }
}

void Cpu::popMemory()
{
  idle();
  readModRmByte();
  std::uint16_t value = 0;
  pop( value );
  setModRm<std::uint16_t>( value );
}

void Cpu::popAll()
{
  idle();
  pop( mState.di );
  pop( mState.si );
  pop( mState.bp );
  std::uint16_t sp = 0;
  pop( sp ); // don't update SP
  pop( mState.bx );
  pop( mState.dx );
  pop( mState.cx );
  pop( mState.ax );
}

void Cpu::pushAll()
{
  idle();
  std::uint16_t const sp = mState.sp;
  push( mState.ax );
  push( mState.cx );
  push( mState.dx );
  push( mState.bx );
  push( sp );
  push( mState.bp );
  push( mState.si );
  push( mState.di );
}

std::uint16_t Cpu::getSegment( Segment defaultSegment )
{
  switch ( mPrefix.segment == Segment::DEFAULT ? defaultSegment : mPrefix.segment )
  {
  default:
  case Segment::ES:
    return mState.es;
  case Segment::SS:
    return mState.ss;
  case Segment::CS:
    return mState.cs;
  case Segment::DS:
    return mState.ds;
  }
}

void Cpu::readModRmByte()
{
  std::uint8_t const modRm = readCodeByte();
  mModRm.mode = ( modRm >> 6U ) & 0x03U;
  mModRm.reg = ( modRm >> 3U ) & 0x07U;
  mModRm.rm = modRm & 0x07U;

  std::uint16_t offset = 0;
  std::uint16_t seg = getSegment( Segment::DS );

  if ( mModRm.mode == 0 && mModRm.rm == 0x06 )
  {
    offset = readCodeWord();
  }
  else if ( mModRm.mode != 3 )
  {
    switch ( mModRm.rm )
    {
    case 0x00:
      idle();
      offset = static_cast<std::uint16_t>( mState.bx + mState.si );
      break;
    case 0x01:
      idle();
      offset = static_cast<std::uint16_t>( mState.bx + mState.di );
      break;
    case 0x02:
      idle();
      offset = static_cast<std::uint16_t>( mState.bp + mState.si );
      seg = getSegment( Segment::SS );
      break;
    case 0x03:
      idle();
      offset = static_cast<std::uint16_t>( mState.bp + mState.di );
      seg = getSegment( Segment::SS );
      break;
    case 0x04:
      offset = mState.si;
      break;
    case 0x05:
      offset = mState.di;
      break;
    case 0x06:
      offset = mState.bp;
      seg = getSegment( Segment::SS );
      break;
    case 0x07:
      offset = mState.bx;
      break;
    default:
      break;
    }

    if ( mModRm.mode == 0x01 )
    {
      offset = static_cast<std::uint16_t>( offset + static_cast<std::int8_t>( readCodeByte() ) );
    }
    else if ( mModRm.mode == 0x02 )
    {
      offset = static_cast<std::uint16_t>( offset + readCodeWord() );
    }
  }

  mModRm.segment = seg;
  mModRm.offset = offset;
}

std::uint16_t Cpu::getModSegRegister( std::uint8_t reg )
{
  return *mModSegLut16[reg & 0x03U];
}

void Cpu::setModSegRegister( std::uint8_t reg, std::uint16_t value )
{
  *mModSegLut16[reg & 0x03U] = value;
}

void Cpu::exchange( std::uint16_t& x, std::uint16_t& y )
{
  idle( 3 );
  std::swap( x, y );
}

void Cpu::processInvalidDiv()
{
  interrupt( 0 );
}

void Cpu::jumpFar()
{
  idle( 6 );
  std::uint16_t const offset = readCodeWord();
  std::uint16_t const segment = readCodeWord();
  mState.cs = segment;
  mState.ip = offset;
  clearPrefetch();
}

void Cpu::jump( bool condition )
{
  idle();
  auto const offset = static_cast<std::int8_t>( readCodeByte() );
  if ( condition )
  {
    idle( 2 );
    mState.ip = static_cast<std::uint16_t>( mState.ip + offset );
    clearPrefetch();
  }
}

void Cpu::jumpNearWord()
{
  idle( 3 );
  std::uint16_t const disp = readCodeWord();
  mState.ip = static_cast<std::uint16_t>( mState.ip + disp );
  clearPrefetch();
}

void Cpu::call( std::uint16_t offset )
{
  push( mState.ip );
  mState.ip = offset;
  clearPrefetch();
}

void Cpu::callNearWord()
{
  idle( 2 );
  std::uint16_t const disp = readCodeWord();
  push( mState.ip );
  mState.ip = static_cast<std::uint16_t>( mState.ip + disp );
  clearPrefetch();
}

void Cpu::callFar()
{
  std::uint16_t const offset = readCodeWord();
  std::uint16_t const segment = readCodeWord();
  push( mState.cs );
  push( mState.ip );
  callFar( segment, offset );
}

void Cpu::callFar( std::uint16_t segment, std::uint16_t offset )
{
  idle( 6 );
  mState.cs = segment;
  mState.ip = offset;
  clearPrefetch();
}

void Cpu::loop()
{
  idle();
  auto const offset = static_cast<std::int8_t>( readCodeByte() );
  mState.cx--;
  if ( mState.cx != 0 )
  {
    idle( 3 );
    mState.ip = static_cast<std::uint16_t>( mState.ip + offset );
    clearPrefetch();
  }
}

void Cpu::loopIf( bool condition )
{
  idle( 2 );
  auto const offset = static_cast<std::int8_t>( readCodeByte() );
  mState.cx--;
  if ( condition && mState.cx != 0 )
  {
    idle( 3 );
    mState.ip = static_cast<std::uint16_t>( mState.ip + offset );
    clearPrefetch();
  }
}

void Cpu::ret()
{
  idle( 4 );
  pop( mState.ip );
  clearPrefetch();
}

void Cpu::retImm()
{
  idle( 4 );
  std::uint16_t const spOffset = readCodeWord();
  pop( mState.ip );
  mState.sp = static_cast<std::uint16_t>( mState.sp + spOffset );
  clearPrefetch();
}

void Cpu::retFar()
{
  idle( 5 );
  pop( mState.ip );
  pop( mState.cs );
  clearPrefetch();
}

void Cpu::retFarImm()
{
  idle( 6 );
  std::uint16_t const spOffset = readCodeWord();
  pop( mState.ip );
  pop( mState.cs );
  mState.sp = static_cast<std::uint16_t>( mState.sp + spOffset );
  clearPrefetch();
}

void Cpu::retInterrupt()
{
  idle( 6 );
  pop( mState.ip );
  pop( mState.cs );
  std::uint16_t flags = 0;
  pop( flags );
  setFlags( flags );
  clearPrefetch();
}

void Cpu::interrupt( std::uint8_t vector, bool pushFirstPrefix )
{
  idle( 30 );

  std::uint16_t const flags = mState.flags.get();
  push( flags );
  push( mState.cs );

  if ( pushFirstPrefix && mPrefix.prefixCount != 0 )
  {
    mState.ip = static_cast<std::uint16_t>( mState.ip - mPrefix.prefixCount );
  }
  mPrefix = {};

  push( mState.ip );

  auto const vectorAddress = static_cast<std::uint16_t>( vector << 2U );
  auto const offset = readMemory<std::uint16_t>( 0, vectorAddress );
  auto const segment = readMemory<std::uint16_t>( 0, static_cast<std::uint16_t>( vectorAddress + 2 ) );

  mState.flags.irq = false;
  mState.flags.trap = false;
  mState.flags.mode = true;
  mState.cs = segment;
  mState.ip = offset;
  clearPrefetch();
}

void Cpu::interruptOverflow()
{
  idle( 6 );
  if ( !mState.flags.overflow )
  {
    return;
  }
  idle( 3 );
  interrupt( 4 );
}

void Cpu::enter()
{
  idle( 7 );

  auto const spOffset = readImmediate<std::uint16_t>();
  auto const pushCount = readImmediate<std::uint8_t>() & 0x1FU;
  push( mState.bp );
  std::uint16_t const sp = mState.sp;
  if ( pushCount > 0 )
  {
    for ( unsigned i = 1; i < pushCount; i++ )
    {
      idle( 2 );
      mState.bp = static_cast<std::uint16_t>( mState.bp - 2 );
      push( readMemory<std::uint16_t>( getSegment( Segment::SS ), mState.bp ) );
    }
    push( sp );
  }
  mState.bp = sp;
  mState.sp = static_cast<std::uint16_t>( mState.sp - spOffset );
}

void Cpu::leave()
{
  idle( 2 );
  mState.sp = mState.bp;
  pop( mState.bp );
}

void Cpu::nop()
{
  idle();
}

void Cpu::fp01()
{
  // 2-byte nop (floating point unit on original V series)
  idle();
  readCodeByte();
}

void Cpu::bound()
{
  idle( 12 );

  readModRmByte();
  auto const min = static_cast<std::int16_t>( getModRm<std::uint16_t>() );
  mModRm.offset = static_cast<std::uint16_t>( mModRm.offset + 2 );
  auto const max = static_cast<std::int16_t>( getModRm<std::uint16_t>() );
  auto const index = static_cast<std::int16_t>( getModRegister<std::uint16_t>( mModRm.reg ) );
  if ( index < min || index > max )
  {
    idle( 3 );
    interrupt( 5 );
  }
}

void Cpu::salc()
{
  idle( 8 );
  mState.ax = static_cast<std::uint16_t>( ( mState.ax & 0xFF00U ) | ( mState.flags.carry ? 0xFFU : 0U ) );
}

void Cpu::cbw()
{
  idle();
  if ( ( mState.ax & 0x80U ) != 0 )
  {
    mState.ax |= 0xFF00U;
  }
  else
  {
    mState.ax &= 0xFFU;
  }
}

void Cpu::cwd()
{
  idle();
  if ( ( mState.ax & 0x8000U ) != 0 )
  {
    mState.dx = 0xFFFF;
  }
  else
  {
    mState.dx = 0;
  }
}

void Cpu::lahf()
{
  idle( 2 );
  mState.ax = static_cast<std::uint16_t>( ( mState.ax & 0xFFU ) | ( ( mState.flags.get() & 0xFFU ) << 8U ) );
}

void Cpu::sahf()
{
  idle( 4 );
  mState.flags.set( static_cast<std::uint16_t>( ( mState.flags.get() & 0xFF00U ) | ( mState.ax >> 8U ) ) );
}

void Cpu::ldsLesLeaModRm()
{
  if ( mModRm.mode == 3 )
  {
    // These instructions change the behavior of mode 3 (reg)
    mModRm.mode = 0;
    std::uint16_t seg = getSegment( Segment::DS );
    std::uint32_t offset = 0;
    switch ( mModRm.rm )
    {
    default:
    case 0x00:
      offset = mState.bx + mState.ax;
      break;
    case 0x01:
      offset = mState.bx + mState.cx;
      break;
    case 0x02:
      offset = mState.bp + mState.dx;
      seg = getSegment( Segment::SS );
      break;
    case 0x03:
      offset = mState.bp + mState.bx;
      seg = getSegment( Segment::SS );
      break;
    case 0x04:
      offset = mState.sp + mState.si;
      break;
    case 0x05:
      offset = mState.bp + mState.di;
      break;
    case 0x06:
      offset = mState.bp + mState.si;
      seg = getSegment( Segment::SS );
      break;
    case 0x07:
      offset = mState.bx + mState.di;
      break;
    }
    mModRm.segment = seg;
    mModRm.offset = static_cast<std::uint16_t>( offset );
  }
}

std::uint16_t Cpu::loadSegment()
{
  idle( 4 );
  readModRmByte();
  ldsLesLeaModRm();

  auto const value = getModRm<std::uint16_t>();
  setModRegister( mModRm.reg, value );

  mModRm.offset = static_cast<std::uint16_t>( mModRm.offset + 2 );
  return getModRm<std::uint16_t>();
}

void Cpu::lds()
{
  mState.ds = loadSegment();
}

void Cpu::les()
{
  mState.es = loadSegment();
}

void Cpu::lea()
{
  idle();
  readModRmByte();
  ldsLesLeaModRm();
  setModRegister( mModRm.reg, mModRm.offset );
}

void Cpu::xlat()
{
  idle( 4 );
  auto const value = readMemory<std::uint8_t>( getSegment( Segment::DS ),
                                               static_cast<std::uint16_t>( mState.bx + ( mState.ax & 0xFFU ) ) );
  mState.ax = static_cast<std::uint16_t>( ( mState.ax & 0xFF00U ) | value );
}

void Cpu::adjustAscii( bool forSub )
{
  idle( 9 );
  auto al = static_cast<std::uint8_t>( mState.ax );
  auto ah = static_cast<std::uint8_t>( mState.ax >> 8U );

  mState.flags.overflow = false;
  mState.flags.parity = true;

  if ( ( al & 0x0FU ) > 9 || mState.flags.auxCarry )
  {
    al = static_cast<std::uint8_t>( al + ( forSub ? -6 : 6 ) );
    ah = static_cast<std::uint8_t>( ah + ( forSub ? -1 : 1 ) );
    mState.flags.auxCarry = true;
  }
  else
  {
    mState.flags.auxCarry = false;
  }

  mState.flags.zero = mState.flags.auxCarry;
  mState.flags.carry = mState.flags.auxCarry;
  mState.flags.sign = !mState.flags.auxCarry;

  al &= 0x0FU;

  mState.ax = static_cast<std::uint16_t>( al | ( ah << 8U ) );
}

void Cpu::aaa()
{
  adjustAscii( false );
}

void Cpu::aas()
{
  adjustAscii( true );
}

void Cpu::aad()
{
  idle( 6 );
  std::uint8_t const imm = readCodeByte();

  auto const ah = static_cast<std::uint8_t>( mState.ax >> 8U );
  auto const al = static_cast<std::uint8_t>( mState.ax );
  mState.ax = add<std::uint8_t>( static_cast<std::uint8_t>( ah * imm ), al, 0 );
}

void Cpu::aam()
{
  idle( 12 );
  std::uint8_t const imm = readCodeByte();
  if ( imm == 0 )
  {
    mState.flags.auxCarry = false;
    mState.flags.parity = false;
    mState.flags.sign = false;
    mState.flags.zero = ( mState.ax & 0xFFU ) <= 0x3F;
    mState.flags.carry = mMulOverflow;
    mState.flags.overflow = mMulOverflow;
    processInvalidDiv();
    return;
  }
  idle( 4 );

  mState.flags.auxCarry = false;
  mState.flags.carry = false;
  mState.flags.overflow = false;

  auto al = static_cast<std::uint8_t>( mState.ax );
  auto const ah = static_cast<std::uint8_t>( al / imm );
  al = static_cast<std::uint8_t>( al % imm );

  mState.ax = static_cast<std::uint16_t>( ( ah << 8U ) | al );
  mState.flags.parity = checkParity( al );
  mState.flags.zero = al == 0;
  mState.flags.sign = ( al & 0x80U ) != 0;
}

void Cpu::adjustDecimal( bool forSub )
{
  auto al = static_cast<std::uint8_t>( mState.ax );
  auto const ah = static_cast<std::uint8_t>( mState.ax >> 8U );

  mState.flags.overflow = false;

  if ( ( al & 0x0FU ) > 9 || mState.flags.auxCarry )
  {
    std::uint8_t const prevAl = al;
    al = static_cast<std::uint8_t>( al + ( forSub ? -6 : 6 ) );
    mState.flags.auxCarry = true;
    mState.flags.overflow = ( ( al ^ prevAl ) & ( ( forSub ? prevAl : al ) ^ 0x06U ) & 0x80U ) != 0;
  }
  else
  {
    mState.flags.auxCarry = false;
  }

  if ( ( mState.ax & 0xFFU ) > 0x99 || mState.flags.carry )
  {
    std::uint8_t const prevAl = al;
    al = static_cast<std::uint8_t>( al + ( forSub ? -0x60 : 0x60 ) );
    mState.flags.carry = true;
    mState.flags.overflow =
        mState.flags.overflow || ( ( al ^ prevAl ) & ( ( forSub ? prevAl : al ) ^ 0x60U ) & 0x80U ) != 0;
  }
  else
  {
    mState.flags.carry = false;
  }

  mState.ax = static_cast<std::uint16_t>( al | ( ah << 8U ) );
  mState.flags.sign = ( al & 0x80U ) != 0;
  mState.flags.zero = al == 0;
  mState.flags.parity = checkParity( al );
}

void Cpu::daa()
{
  idle( 10 );
  adjustDecimal( false );
}

void Cpu::das()
{
  idle( 11 );
  adjustDecimal( true );
}

void Cpu::undefined()
{
  idle();
}

void Cpu::wait()
{
  idle( 10 );
}

void Cpu::halt()
{
  idle( 12 );
  mState.halted = true;
  if ( mm().isPowerOffRequested() )
  {
    mState.powerOff = true;
  }
}

void Cpu::suppressIrq( bool suppressTrap )
{
  // When the IRQ flag gets set, or when the SS segment is modified, IRQs
  // are suppressed after the current instruction and will only trigger
  // on the instruction after that.
  mSuppressIrqClock = mState.cycleCount;
  if ( suppressTrap )
  {
    mSuppressTrapClock = mState.cycleCount;
  }
}

void Cpu::setFlagValue( bool& flag, bool value )
{
  idle( 4 );
  flag = value;
}

void Cpu::setIrqFlag()
{
  idle( 4 );
  if ( !mState.flags.irq )
  {
    mState.flags.irq = true;
    // Suppress IRQs for the next instruction when irq flag gets set
    suppressIrq( false );
  }
}

void Cpu::clearPrefetch()
{
  mPrefetch.clear( mState.cs, mState.ip );
}

template <typename T>
T Cpu::getModRegister( std::uint8_t reg )
{
  if constexpr ( IS_WORD<T> )
  {
    return *mModRegLut16[reg];
  }
  else
  {
    std::uint16_t const regValue = *mModRegLut8[reg & 0x03U];
    if ( ( reg & 0x04U ) != 0 )
    {
      return static_cast<std::uint8_t>( regValue >> 8U );
    }
    return static_cast<std::uint8_t>( regValue );
  }
}

template <typename T>
void Cpu::setModRegister( std::uint8_t reg, T value )
{
  if constexpr ( IS_WORD<T> )
  {
    *mModRegLut16[reg] = value;
  }
  else
  {
    std::uint16_t& regValue = *mModRegLut8[reg & 0x03U];
    if ( ( reg & 0x04U ) != 0 )
    {
      regValue = static_cast<std::uint16_t>( ( regValue & 0xFFU ) | ( static_cast<std::uint32_t>( value ) << 8U ) );
    }
    else
    {
      regValue = static_cast<std::uint16_t>( ( regValue & 0xFF00U ) | value );
    }
  }
}

template <typename T>
T Cpu::getModRm()
{
  if ( mModRm.mode == 0x03 )
  {
    return getModRegister<T>( mModRm.rm );
  }

  return readMemory<T>( mModRm.segment, mModRm.offset );
}

template <typename T>
void Cpu::setModRm( T value )
{
  if ( mModRm.mode == 0x03 )
  {
    setModRegister( mModRm.rm, value );
  }
  else
  {
    writeMemory<T>( mModRm.segment, mModRm.offset, value );
  }
}

template <bool SIGN, typename T>
void Cpu::grp1ModRm()
{
  idle();

  readModRmByte();

  T const param1 = getModRm<T>();
  std::uint16_t param2 = IS_WORD<T> && !SIGN ? readCodeWord() : readCodeByte();
  if constexpr ( IS_WORD<T> && SIGN )
  {
    // The byte, sign-extended to a word.
    param2 = static_cast<std::uint16_t>( ( param2 ^ 0x80U ) - 0x80U );
  }

  auto const op = static_cast<AluOp>( mModRm.reg );
  T const result = getAluResult<T>( op, param1, static_cast<T>( param2 ) );
  if ( op != AluOp::CMP )
  {
    setModRm( result );
  }
}

template <typename T, Cpu::Grp2Mode MODE>
void Cpu::grp2ModRm()
{
  readModRmByte();

  T const param = getModRm<T>();
  std::uint8_t shift = 0;
  if constexpr ( MODE == Grp2Mode::ONE )
  {
    shift = 1;
    idle();
  }
  else if constexpr ( MODE == Grp2Mode::CL )
  {
    shift = static_cast<std::uint8_t>( mState.cx );
    idle( 3 );
  }
  else
  {
    shift = readCodeByte();
    idle( 3 );
  }

  shift &= 0x1FU;

  T result = 0;
  switch ( mModRm.reg )
  {
  default:
  case 0x00:
    result = rol( param, shift );
    break;
  case 0x01:
    result = ror( param, shift );
    break;
  case 0x02:
    result = rcl( param, shift );
    break;
  case 0x03:
    result = rcr( param, shift );
    break;
  case 0x04:
    result = shl( param, shift );
    break;
  case 0x05:
    result = shr( param, shift );
    break;
  case 0x06:
    result = 0;
    break;
  case 0x07:
    result = sar( param, shift );
    break;
  }
  setModRm( result );
}

template <typename T>
void Cpu::grp3ModRm()
{
  readModRmByte();

  T param = getModRm<T>();

  idle();

  switch ( mModRm.reg )
  {
  case 0x00:
    bitAnd<T>( param, readImmediate<T>() );
    break; // TEST
  case 0x01:
    break; // NOP
  case 0x02:
    param = static_cast<T>( ~param );
    setModRm( param );
    break;
  case 0x03:
    param = sub<T>( 0, param, 0 );
    setModRm( param );
    break; // NEG
  case 0x04:
    idle( 2 );
    mulUnsigned<T>( static_cast<T>( mState.ax ), param );
    break;
  case 0x05:
    idle( 2 );
    mulSigned<T>( static_cast<T>( mState.ax ), param );
    break;
  case 0x06:
    divUnsigned<T>( param );
    break;
  case 0x07:
    divSigned<T>( param );
    break;
  default:
    break;
  }
}

template <typename T>
void Cpu::grp45ModRm()
{
  readModRmByte();

  switch ( mModRm.reg )
  {
  case 0x00:
  {
    T result = getModRm<T>();
    inc<T>( result );
    setModRm( result );
    break;
  }

  case 0x01:
  {
    T result = getModRm<T>();
    dec<T>( result );
    setModRm( result );
    break;
  }

  case 0x02:
    idle( 2 );
    call( getModRm<std::uint16_t>() );
    break;

  case 0x03:
  {
    push( mState.cs );
    push( mState.ip );

    ldsLesLeaModRm();
    auto const offset = getModRm<std::uint16_t>();
    mModRm.offset = static_cast<std::uint16_t>( mModRm.offset + 2 );
    auto const segment = getModRm<std::uint16_t>();
    callFar( segment, offset );
    break;
  }

  case 0x04:
  {
    idle( 4 );
    auto const offset = getModRm<std::uint16_t>();
    mState.ip = offset;
    clearPrefetch();
    break;
  }

  case 0x05:
  {
    ldsLesLeaModRm();
    idle( 7 );
    auto const offset = getModRm<std::uint16_t>();
    mModRm.offset = static_cast<std::uint16_t>( mModRm.offset + 2 );
    auto const segment = getModRm<std::uint16_t>();
    mState.cs = segment;
    mState.ip = offset;
    clearPrefetch();
    break;
  }

  case 0x06:
    push( getModRm<std::uint16_t>() );
    break;

  case 0x07:
  default:
    undefined();
    break;
  }
}

template <typename T>
void Cpu::testModRm()
{
  idle();

  readModRmByte();

  T const param1 = getModRm<T>();
  T const param2 = getModRegister<T>( mModRm.reg );

  bitAnd<T>( param1, param2 );
}

template <typename T>
void Cpu::testImmediate()
{
  idle();
  T const imm = readImmediate<T>();
  bitAnd<T>( static_cast<T>( mState.ax ), imm );
}

template <typename T>
void Cpu::exchangeModRm()
{
  idle( 3 );
  readModRmByte();

  T const param1 = getModRm<T>();
  T const param2 = getModRegister<T>( mModRm.reg );

  setModRegister( mModRm.reg, param1 );
  setModRm( param2 );
}

template <bool DIRECTION, typename T>
void Cpu::moveModRm()
{
  readModRmByte();

  if ( mModRm.mode == 0x03 )
  {
    idle();
  }

  if constexpr ( DIRECTION )
  {
    T const param = getModRm<T>();
    setModRegister( mModRm.reg, param );
  }
  else
  {
    T const param = getModRegister<T>( mModRm.reg );
    setModRm( param );
  }
}

template <bool DIRECTION>
void Cpu::moveSegment()
{
  idle( DIRECTION ? 2 : 1 );

  readModRmByte();

  if constexpr ( DIRECTION )
  {
    auto const param = getModRm<std::uint16_t>();
    setModSegRegister( mModRm.reg, param );
    if ( mModRm.reg == 2 )
    {
      // SS was updated, suppress IRQs after this instruction
      suppressIrq( true );
    }
  }
  else
  {
    setModRm( getModSegRegister( mModRm.reg ) );
  }
}

template <bool DIRECTION, typename T>
void Cpu::moveAccumulator()
{
  std::uint16_t const offset = readCodeWord();
  if constexpr ( DIRECTION )
  {
    auto const param = static_cast<T>( mState.ax );
    writeMemory<T>( getSegment( Segment::DS ), offset, param );
  }
  else
  {
    T const param = readMemory<T>( getSegment( Segment::DS ), offset );
    // Write to AL/AX
    setModRegister<T>( 0, param );
  }
}

template <typename T>
void Cpu::moveImmediate()
{
  readModRmByte();
  T const imm = readImmediate<T>();
  setModRm( imm );
}

template <Cpu::AluOp OP, bool DIRECTION, typename T>
void Cpu::processAluModRm()
{
  idle();

  readModRmByte();
  T param1 = 0;
  T param2 = 0;
  if constexpr ( DIRECTION )
  {
    param1 = getModRegister<T>( mModRm.reg );
    param2 = getModRm<T>();
  }
  else
  {
    param1 = getModRm<T>();
    param2 = getModRegister<T>( mModRm.reg );
  }

  T const result = getAluResult<T>( OP, param1, param2 );
  if constexpr ( OP != AluOp::CMP )
  {
    if constexpr ( DIRECTION )
    {
      setModRegister( mModRm.reg, result );
    }
    else
    {
      setModRm( result );
    }
  }
}

template <Cpu::AluOp OP, typename T>
void Cpu::processAluImm()
{
  idle();

  auto const param1 = static_cast<T>( mState.ax );
  T const param2 = readImmediate<T>();

  T const result = getAluResult<T>( OP, param1, param2 );

  if constexpr ( OP != AluOp::CMP )
  {
    setModRegister<T>( 0, result );
  }
}

template <typename T>
T Cpu::getAluResult( AluOp op, T param1, T param2 )
{
  switch ( op )
  {
  default:
  case AluOp::ADD:
    return add<T>( param1, param2, 0 );
  case AluOp::OR:
    return bitOr<T>( param1, param2 );
  case AluOp::ADC:
    return add<T>( param1, param2, mState.flags.carry );
  case AluOp::SBB:
    return sub<T>( param1, param2, mState.flags.carry );
  case AluOp::AND:
    return bitAnd<T>( param1, param2 );
  case AluOp::SUB:
    return sub<T>( param1, param2, 0 );
  case AluOp::XOR:
    return bitXor<T>( param1, param2 );
  case AluOp::CMP:
    return sub<T>( param1, param2, 0 );
  }
}

template <typename T>
void Cpu::inc( T& dst )
{
  idle();

  auto const result = static_cast<T>( dst + 1 );
  mState.flags.zero = result == 0;
  mState.flags.auxCarry = ( dst & 0x0FU ) == 0x0F;
  mState.flags.overflow = result == getSign<T>();
  mState.flags.sign = ( result & getSign<T>() ) != 0;
  mState.flags.parity = checkParity( result );
  dst = result;
}

template <typename T>
void Cpu::dec( T& dst )
{
  idle();

  auto const result = static_cast<T>( dst - 1 );
  mState.flags.zero = result == 0;
  mState.flags.auxCarry = ( dst & 0x0FU ) == 0;
  mState.flags.overflow = result == getSign<T>() - 1;
  mState.flags.sign = ( result & getSign<T>() ) != 0;
  mState.flags.parity = checkParity( result );
  dst = result;
}

template <typename T>
T Cpu::add( T x, T y, std::uint8_t carry )
{
  std::uint32_t result = static_cast<std::uint32_t>( x ) + y + carry;

  mState.flags.carry = result > getMaxValue<T>();
  result &= getMaxValue<T>();

  mState.flags.zero = result == 0;
  mState.flags.auxCarry = ( x & 0x0FU ) + ( y & 0x0FU ) + carry >= 0x10;
  mState.flags.overflow = ( ( result ^ x ) & ( result ^ y ) & getSign<T>() ) != 0;
  mState.flags.sign = ( result & getSign<T>() ) != 0;
  mState.flags.parity = checkParity( result );

  return static_cast<T>( result );
}

template <typename T>
T Cpu::sub( T x, T y, std::uint8_t borrow )
{
  std::int32_t result = static_cast<std::int32_t>( x ) - static_cast<std::int32_t>( y ) - borrow;

  mState.flags.carry = result < 0;
  result &= static_cast<std::int32_t>( getMaxValue<T>() );

  mState.flags.zero = result == 0;
  mState.flags.auxCarry = static_cast<int>( x & 0x0FU ) - static_cast<int>( y & 0x0FU ) - borrow < 0;
  mState.flags.overflow = ( ( x ^ y ) & ( x ^ result ) & static_cast<std::int32_t>( getSign<T>() ) ) != 0;
  mState.flags.sign = ( result & static_cast<std::int32_t>( getSign<T>() ) ) != 0;
  mState.flags.parity = checkParity( static_cast<std::uint8_t>( result ) );

  return static_cast<T>( result );
}

template <typename T>
void Cpu::multSignedModRm()
{
  idle( 3 );
  readModRmByte();

  std::int16_t param1 = 0;
  if constexpr ( IS_WORD<T> )
  {
    param1 = static_cast<std::int16_t>( readImmediate<T>() );
  }
  else
  {
    // The byte, sign-extended.
    param1 = static_cast<std::int16_t>( ( readImmediate<T>() ^ 0x80 ) - 0x80 );
  }
  auto const param2 = getModRm<std::uint16_t>();

  std::int32_t const result = getMultiplyResult<std::uint16_t>( static_cast<std::uint16_t>( param1 ), param2 );
  setModRegister<std::uint16_t>( mModRm.reg, static_cast<std::uint16_t>( result ) );
}

template <typename T>
void Cpu::mulSigned( T x, T y )
{
  std::int32_t const result = getMultiplyResult( x, y );
  if constexpr ( IS_WORD<T> )
  {
    mState.ax = static_cast<std::uint16_t>( result );
    mState.dx = static_cast<std::uint16_t>( result >> 16 );
  }
  else
  {
    mState.ax = static_cast<std::uint16_t>( result );
  }
}

template <typename T>
std::int32_t Cpu::getMultiplyResult( T x, T y )
{
  std::int32_t result = 0;
  bool overflow = false;
  if constexpr ( IS_WORD<T> )
  {
    result = static_cast<std::int16_t>( x ) * static_cast<std::int16_t>( y );
    overflow = result != static_cast<std::int16_t>( result );
  }
  else
  {
    result = static_cast<std::int8_t>( x ) * static_cast<std::int8_t>( y );
    overflow = result != static_cast<std::int8_t>( result );
  }

  mState.flags.carry = overflow;
  mState.flags.overflow = overflow;
  mState.flags.auxCarry = false;
  mState.flags.parity = false;
  mState.flags.sign = false;
  mState.flags.zero = true;
  mMulOverflow = overflow;

  return result;
}

template <typename T>
void Cpu::mulUnsigned( T x, T y )
{
  std::uint32_t const result = static_cast<std::uint32_t>( x ) * y;
  bool const overflow = result > getMaxValue<T>();
  mState.flags.carry = overflow;
  mState.flags.overflow = overflow;
  mState.flags.auxCarry = false;
  mState.flags.parity = false;
  mState.flags.sign = false;
  mState.flags.zero = mSystem->getModel() != Model::MONOCHROME;

  mMulOverflow = overflow;

  if constexpr ( IS_WORD<T> )
  {
    mState.ax = static_cast<std::uint16_t>( result );
    mState.dx = static_cast<std::uint16_t>( result >> 16U );
  }
  else
  {
    mState.ax = static_cast<std::uint16_t>( result );
  }
}

template <typename T>
void Cpu::divSigned( T y )
{
  idle( 14 );

  if constexpr ( IS_WORD<T> )
  {
    mState.flags.carry = false;
    mState.flags.overflow = false;
    mState.flags.auxCarry = false;
    mState.flags.parity = false;
    mState.flags.sign = false;

    if ( mState.dx == 0x8000 && mState.ax == 0 && y == 0 )
    {
      // CPU bug? 0x80000000 / 0x0000 does not trigger a divide by 0 error
      mState.ax = 0x8001;
      mState.dx = 0;
      mState.flags.zero = true;
      return;
    }
  }
  else
  {
    mState.flags.auxCarry = false;
    mState.flags.carry = mMulOverflow;
    mState.flags.overflow = mMulOverflow;

    if ( mState.ax == 0x8000 && y == 0 )
    {
      // CPU bug? 0x8000 / 0x00 does not trigger a divide by 0 error
      mState.ax = 0x81;
      mState.flags.zero = false;
      mState.flags.parity = true;
      mState.flags.sign = true;
      mState.flags.carry = false;
      mState.flags.overflow = false;
      return;
    }
  }

  if ( y == 0 )
  {
    mState.flags.parity = false;
    mState.flags.sign = false;
    processInvalidDiv();
    return;
  }

  idle( IS_WORD<T> ? 9 : 2 );

  // In 64 bits: Mesen2's 32-bit 0x80000000 / -1 overflows (and traps on x86);
  // its quotient is caught as too large below either way.
  std::int64_t result = 0;
  std::int64_t mod = 0;
  bool overflow = false;
  if constexpr ( IS_WORD<T> )
  {
    auto const x = static_cast<std::int32_t>( ( static_cast<std::uint32_t>( mState.dx ) << 16U ) | mState.ax );
    result = static_cast<std::int64_t>( x ) / static_cast<std::int16_t>( y );
    mod = static_cast<std::int64_t>( x ) % static_cast<std::int16_t>( y );
    overflow = result > INT16_MAX || result < -INT16_MAX;
  }
  else
  {
    result = static_cast<std::int16_t>( mState.ax ) / static_cast<std::int8_t>( y );
    mod = static_cast<std::int16_t>( mState.ax ) % static_cast<std::int8_t>( y );
    overflow = result > INT8_MAX || result < -INT8_MAX;
  }

  if ( overflow )
  {
    // Result too large
    mState.flags.parity = false;
    mState.flags.sign = false;
    processInvalidDiv();
    return;
  }

  if constexpr ( IS_WORD<T> )
  {
    mState.ax = static_cast<std::uint16_t>( result );
    mState.dx = static_cast<std::uint16_t>( mod );
    mState.flags.zero = mod == 0 && ( result & 0x01 ) != 0;
  }
  else
  {
    mState.ax =
        static_cast<std::uint16_t>( ( static_cast<std::uint8_t>( mod ) << 8U ) | static_cast<std::uint8_t>( result ) );

    auto const al = static_cast<std::uint8_t>( mState.ax );
    mState.flags.parity = checkParity( al );
    mState.flags.zero = al == 0;
    mState.flags.sign = ( al & 0x80U ) != 0;
    mState.flags.carry = false;
    mState.flags.overflow = false;
  }
}

template <typename T>
void Cpu::divUnsigned( T y )
{
  idle( 11 );
  mState.flags.auxCarry = false;
  mState.flags.parity = false;
  mState.flags.sign = false;

  if constexpr ( IS_WORD<T> )
  {
    mState.flags.carry = false;
    mState.flags.overflow = false;
  }
  else
  {
    mState.flags.carry = mMulOverflow;
    mState.flags.overflow = mMulOverflow;
  }

  if ( y == 0 )
  {
    processInvalidDiv();
    return;
  }

  idle( IS_WORD<T> ? 11 : 3 );

  std::uint32_t x = 0;
  if constexpr ( IS_WORD<T> )
  {
    x = ( static_cast<std::uint32_t>( mState.dx ) << 16U ) | mState.ax;
  }
  else
  {
    x = mState.ax;
  }

  std::uint32_t const result = x / y;
  std::uint32_t const mod = x % y;

  if ( result > getMaxValue<T>() )
  {
    // Result too large
    processInvalidDiv();
    return;
  }

  if constexpr ( IS_WORD<T> )
  {
    mState.ax = static_cast<std::uint16_t>( result );
    mState.dx = static_cast<std::uint16_t>( mod );
  }
  else
  {
    mState.ax =
        static_cast<std::uint16_t>( ( static_cast<std::uint8_t>( mod ) << 8U ) | static_cast<std::uint8_t>( result ) );
  }

  mState.flags.zero = mod == 0 && ( result & 0x01U ) != 0;
}

template <typename T>
void Cpu::updateFlags( T result )
{
  mState.flags.carry = false;
  mState.flags.overflow = false;
  mState.flags.auxCarry = false;
  mState.flags.zero = result == 0;
  mState.flags.sign = ( result & getSign<T>() ) != 0;
  mState.flags.parity = checkParity( result );
}

template <typename T>
T Cpu::bitAnd( T x, T y )
{
  auto const result = static_cast<T>( x & y );
  updateFlags<T>( result );
  return result;
}

template <typename T>
T Cpu::bitOr( T x, T y )
{
  auto const result = static_cast<T>( x | y );
  updateFlags<T>( result );
  return result;
}

template <typename T>
T Cpu::bitXor( T x, T y )
{
  auto const result = static_cast<T>( x ^ y );
  updateFlags<T>( result );
  return result;
}

template <typename T>
T Cpu::rol( T x, std::uint8_t shift )
{
  std::uint32_t const s = shift % getBitCount<T>();
  std::uint32_t const wide = x;
  auto const result = static_cast<T>( s == 0 ? wide : ( ( wide << s ) | ( wide >> ( getBitCount<T>() - s ) ) ) );
  if ( shift != 0 )
  {
    mState.flags.carry = ( result & 0x01U ) != 0;
  }
  mState.flags.overflow =
      ( ( result ^ ( static_cast<std::uint32_t>( mState.flags.carry ) << ( getBitCount<T>() - 1 ) ) ) &
        getSign<T>() ) != 0;
  return result;
}

template <typename T>
T Cpu::ror( T x, std::uint8_t shift )
{
  std::uint32_t const s = shift % getBitCount<T>();
  std::uint32_t const wide = x;
  auto const result = static_cast<T>( s == 0 ? wide : ( ( wide >> s ) | ( wide << ( getBitCount<T>() - s ) ) ) );
  if ( shift != 0 )
  {
    mState.flags.carry = ( result & getSign<T>() ) != 0;
  }
  mState.flags.overflow = ( ( result ^ ( static_cast<std::uint32_t>( result ) << 1U ) ) & getSign<T>() ) != 0;
  return result;
}

template <typename T>
T Cpu::rcl( T x, std::uint8_t shift )
{
  std::uint32_t const s = shift % ( getBitCount<T>() + 1 );
  std::uint64_t const wide = x;
  std::uint64_t const result =
      shift == 0 ? wide
                 : ( ( wide << s ) | ( wide >> ( getBitCount<T>() - s + 1 ) ) |
                     ( static_cast<std::uint64_t>( mState.flags.carry ) << ( s > 0 ? ( s - 1 ) : getBitCount<T>() ) ) );
  if ( shift != 0 )
  {
    mState.flags.carry = ( ( result >> getBitCount<T>() ) & 0x01U ) != 0;
  }
  mState.flags.overflow =
      ( ( result ^ ( static_cast<std::uint64_t>( mState.flags.carry ) << ( getBitCount<T>() - 1 ) ) ) &
        getSign<T>() ) != 0;
  return static_cast<T>( result );
}

template <typename T>
T Cpu::rcr( T x, std::uint8_t shift )
{
  std::uint32_t const s = shift % ( getBitCount<T>() + 1 );
  std::uint64_t const wide = x;
  std::uint64_t const result =
      shift == 0 ? wide
                 : ( ( wide >> s ) | ( wide << ( getBitCount<T>() - s + 1 ) ) |
                     ( static_cast<std::uint64_t>( mState.flags.carry ) << ( getBitCount<T>() - s ) ) );
  if ( shift != 0 )
  {
    mState.flags.carry = ( ( result >> getBitCount<T>() ) & 0x01U ) != 0;
  }
  mState.flags.overflow = ( ( result ^ ( result << 1U ) ) & getSign<T>() ) != 0;
  return static_cast<T>( result );
}

template <typename T>
T Cpu::shl( T x, std::uint8_t shift )
{
  std::uint32_t const result = static_cast<std::uint32_t>( x ) << shift;
  bool const carry = mState.flags.carry;
  updateFlags<T>( static_cast<T>( result ) );

  if ( shift == 0 )
  {
    mState.flags.carry = carry;
  }
  else
  {
    mState.flags.carry = ( result & ( 1U << getBitCount<T>() ) ) != 0;
  }

  mState.flags.overflow =
      ( ( result ^ ( static_cast<std::uint32_t>( mState.flags.carry ) << ( getBitCount<T>() - 1 ) ) ) &
        getSign<T>() ) != 0;

  return static_cast<T>( result );
}

template <typename T>
T Cpu::shr( T x, std::uint8_t shift )
{
  std::uint64_t const result = static_cast<std::uint64_t>( x ) >> shift;
  bool const carry = mState.flags.carry;
  updateFlags<T>( static_cast<T>( result ) );

  if ( shift == 0 )
  {
    mState.flags.carry = carry;
  }
  else
  {
    mState.flags.carry = ( ( x >> ( shift - 1 ) ) & 0x01U ) != 0;
  }

  mState.flags.overflow = ( ( result ^ ( result << 1U ) ) & getSign<T>() ) != 0;

  return static_cast<T>( result );
}

template <typename T>
T Cpu::sar( T x, std::uint8_t shift )
{
  using Signed = std::conditional_t<IS_WORD<T>, std::int16_t, std::int8_t>;
  auto const result = static_cast<T>( static_cast<Signed>( x ) >> shift );
  // Mesen2 also takes this when the shift is 0, a shift by -1; it is not used then.
  bool const carry = shift != 0 && ( ( static_cast<Signed>( x ) >> ( shift - 1 ) ) & 0x01 ) != 0;

  bool const orgCarry = mState.flags.carry;
  updateFlags<T>( result );
  mState.flags.carry = shift != 0 ? carry : orgCarry;
  mState.flags.overflow = ( ( result ^ ( static_cast<std::uint32_t>( result ) << 1U ) ) & getSign<T>() ) != 0;

  return result;
}

template <typename T>
void Cpu::out( std::uint8_t delay, std::uint16_t port, T data )
{
  idle( delay );
  writePort<T>( port, data );
}

template <typename T, bool IS_DX_PORT>
void Cpu::inStoreAx( std::uint16_t port )
{
  idle( IS_DX_PORT ? 4 : 5 );
  if constexpr ( IS_WORD<T> )
  {
    mState.ax = readPort<std::uint16_t>( port );
  }
  else
  {
    mState.ax = static_cast<std::uint16_t>( ( mState.ax & 0xFF00U ) | readPort<std::uint8_t>( port ) );
  }

  // TODOWS
  // The gdma_timing test seems to imply that IN instruciton might potentially
  // be reading the port a cycle early and then spends an extra cycle
  // on something else (e.g potentially updating AX?), but this needs more
  // research/testing. This allows the gdma_timing test to pass.
  idle();
}

template <typename T>
std::uint16_t Cpu::getStringStep() const
{
  return static_cast<std::uint16_t>( mState.flags.direction ? 0x10000U - sizeof( T ) : sizeof( T ) );
}

template <typename T>
void Cpu::processStringOperation( bool incSi, bool incDi )
{
  if ( incSi )
  {
    mState.si = static_cast<std::uint16_t>( mState.si + getStringStep<T>() );
  }

  if ( incDi )
  {
    mState.di = static_cast<std::uint16_t>( mState.di + getStringStep<T>() );
  }

  if ( mPrefix.rep != RepMode::NONE )
  {
    mState.cx--;
    if ( mState.cx != 0 )
    {
      // Re-execute the same instruction again
      mPrefetch.processRep( mOpCode );
      mState.ip--;
      mPrefix.preserve = true;
    }
  }
}

template <typename T>
void Cpu::processStringCmpOperation( bool incSi )
{
  if ( incSi )
  {
    mState.si = static_cast<std::uint16_t>( mState.si + getStringStep<T>() );
  }

  mState.di = static_cast<std::uint16_t>( mState.di + getStringStep<T>() );
  if ( mPrefix.rep != RepMode::NONE )
  {
    mState.cx--;
    if ( mState.cx != 0 && ( mPrefix.rep != RepMode::NOT_ZERO || !mState.flags.zero ) &&
         ( mPrefix.rep != RepMode::ZERO || mState.flags.zero ) )
    {
      // Re-execute the same instruction again
      mPrefetch.processRep( mOpCode );
      mState.ip--;
      mPrefix.preserve = true;
    }
  }
}

template <typename T>
void Cpu::ins()
{
  if ( !mPrefix.preserve && mPrefix.rep != RepMode::NONE )
  {
    // First iteration
    idle( 5 );
  }

  mPrefix.preserve = false;
  if ( mPrefix.rep == RepMode::NONE || mState.cx > 0 )
  {
    if ( mPrefix.rep == RepMode::NONE )
    {
      idle( 3 );
    }
    else
    {
      idle( 4 );
    }

    T const value = readPort<T>( mState.dx );
    writeMemory<T>( mState.es, mState.di, value );
    processStringOperation<T>( false, true );
  }
}

template <typename T>
void Cpu::outs()
{
  if ( !mPrefix.preserve && mPrefix.rep != RepMode::NONE )
  {
    // First iteration
    idle( 5 );
  }

  mPrefix.preserve = false;
  if ( mPrefix.rep == RepMode::NONE || mState.cx > 0 )
  {
    if ( mPrefix.rep == RepMode::NONE )
    {
      idle( 3 );
    }
    else
    {
      idle( 4 );
    }

    T const value = readMemory<T>( getSegment( Segment::DS ), mState.si );
    out<T>( 0, mState.dx, value );
    processStringOperation<T>( true, false );
  }
}

template <typename T>
void Cpu::movs()
{
  if ( !mPrefix.preserve && mPrefix.rep != RepMode::NONE )
  {
    // First iteration
    idle( 5 );
  }

  mPrefix.preserve = false;
  if ( mPrefix.rep == RepMode::NONE || mState.cx > 0 )
  {
    if ( mPrefix.rep == RepMode::NONE )
    {
      idle( 3 );
    }
    else
    {
      idle( 5 );
    }

    T const value = readMemory<T>( getSegment( Segment::DS ), mState.si );
    writeMemory<T>( mState.es, mState.di, value );
    processStringOperation<T>( true, true );
  }
}

template <typename T>
void Cpu::stos()
{
  if ( !mPrefix.preserve && mPrefix.rep != RepMode::NONE )
  {
    // First iteration
    idle( 5 );
  }

  mPrefix.preserve = false;
  if ( mPrefix.rep == RepMode::NONE || mState.cx > 0 )
  {
    if ( mPrefix.rep == RepMode::NONE )
    {
      idle( 2 );
    }
    else
    {
      idle( 5 );
    }

    writeMemory<T>( mState.es, mState.di, static_cast<T>( mState.ax ) );
    processStringOperation<T>( false, true );
  }
}

template <typename T>
void Cpu::lods()
{
  if ( !mPrefix.preserve && mPrefix.rep != RepMode::NONE )
  {
    // First iteration
    idle( 5 );
  }

  mPrefix.preserve = false;
  if ( mPrefix.rep == RepMode::NONE || mState.cx > 0 )
  {
    if ( mPrefix.rep == RepMode::NONE )
    {
      idle( 2 );
    }
    else
    {
      idle( 5 );
    }

    T const value = readMemory<T>( getSegment( Segment::DS ), mState.si );
    if constexpr ( IS_WORD<T> )
    {
      mState.ax = value;
    }
    else
    {
      mState.ax = static_cast<std::uint16_t>( ( mState.ax & 0xFF00U ) | value );
    }
    processStringOperation<T>( true, false );
  }
}

template <typename T>
void Cpu::cmps()
{
  if ( !mPrefix.preserve && mPrefix.rep != RepMode::NONE )
  {
    // First iteration
    idle( 5 );
  }

  mPrefix.preserve = false;
  if ( mPrefix.rep == RepMode::NONE || mState.cx > 0 )
  {
    if ( mPrefix.rep == RepMode::NONE )
    {
      idle( 4 );
    }
    else
    {
      idle( 8 );
    }

    T const param1 = readMemory<T>( getSegment( Segment::DS ), mState.si );
    T const param2 = readMemory<T>( mState.es, mState.di );
    sub<T>( param1, param2, 0 );
    processStringCmpOperation<T>( true );
  }
}

template <typename T>
void Cpu::scas()
{
  if ( !mPrefix.preserve && mPrefix.rep != RepMode::NONE )
  {
    // First iteration
    idle( 5 );
  }

  mPrefix.preserve = false;
  if ( mPrefix.rep == RepMode::NONE || mState.cx > 0 )
  {
    if ( mPrefix.rep == RepMode::NONE )
    {
      idle( 3 );
    }
    else
    {
      idle( 8 );
    }

    T const param = readMemory<T>( mState.es, mState.di );
    sub<T>( static_cast<T>( mState.ax ), param, 0 );
    processStringCmpOperation<T>( false );
  }
}

bool Cpu::processPrefix( std::uint8_t opCode )
{
  switch ( opCode )
  {
  case 0x26:
    mPrefix.segment = Segment::ES;
    break;
  case 0x2E:
    mPrefix.segment = Segment::CS;
    break;
  case 0x36:
    mPrefix.segment = Segment::SS;
    break;
  case 0x3E:
    mPrefix.segment = Segment::DS;
    break;
  case 0xF0:
    mPrefix.lock = true;
    break;
  case 0xF2:
    mPrefix.rep = RepMode::NOT_ZERO;
    break;
  case 0xF3:
    mPrefix.rep = RepMode::ZERO;
    break;
  default:
    return false;
  }
  mPrefix.prefixCount++;
  return true;
}

void Cpu::execOpCode()
{
  std::uint8_t opCode = readCodeByte();
  while ( processPrefix( opCode ) )
  {
    opCode = readCodeByte();
  }

  mOpCode = opCode;
  // The opcode table, an opcode a line as Mesen2 has it: the undefined and
  // FPU escapes repeat on purpose, so that the table reads as the opcode map.
  // clang-format off
  // NOLINTBEGIN(bugprone-branch-clone)
  switch ( opCode )
  {
  case 0x00: processAluModRm<AluOp::ADD, false, std::uint8_t>(); break;
  case 0x01: processAluModRm<AluOp::ADD, false, std::uint16_t>(); break;
  case 0x02: processAluModRm<AluOp::ADD, true, std::uint8_t>(); break;
  case 0x03: processAluModRm<AluOp::ADD, true, std::uint16_t>(); break;
  case 0x04: processAluImm<AluOp::ADD, std::uint8_t>(); break;
  case 0x05: processAluImm<AluOp::ADD, std::uint16_t>(); break;
  case 0x06: pushSegment( mState.es ); break;
  case 0x07: popSegment( mState.es ); break;
  case 0x08: processAluModRm<AluOp::OR, false, std::uint8_t>(); break;
  case 0x09: processAluModRm<AluOp::OR, false, std::uint16_t>(); break;
  case 0x0A: processAluModRm<AluOp::OR, true, std::uint8_t>(); break;
  case 0x0B: processAluModRm<AluOp::OR, true, std::uint16_t>(); break;
  case 0x0C: processAluImm<AluOp::OR, std::uint8_t>(); break;
  case 0x0D: processAluImm<AluOp::OR, std::uint16_t>(); break;
  case 0x0E: pushSegment( mState.cs ); break;
  case 0x0F: undefined(); break;

  case 0x10: processAluModRm<AluOp::ADC, false, std::uint8_t>(); break;
  case 0x11: processAluModRm<AluOp::ADC, false, std::uint16_t>(); break;
  case 0x12: processAluModRm<AluOp::ADC, true, std::uint8_t>(); break;
  case 0x13: processAluModRm<AluOp::ADC, true, std::uint16_t>(); break;
  case 0x14: processAluImm<AluOp::ADC, std::uint8_t>(); break;
  case 0x15: processAluImm<AluOp::ADC, std::uint16_t>(); break;
  case 0x16: pushSegment( mState.ss ); break;
  case 0x17: popSegment( mState.ss ); suppressIrq( true ); break;
  case 0x18: processAluModRm<AluOp::SBB, false, std::uint8_t>(); break;
  case 0x19: processAluModRm<AluOp::SBB, false, std::uint16_t>(); break;
  case 0x1A: processAluModRm<AluOp::SBB, true, std::uint8_t>(); break;
  case 0x1B: processAluModRm<AluOp::SBB, true, std::uint16_t>(); break;
  case 0x1C: processAluImm<AluOp::SBB, std::uint8_t>(); break;
  case 0x1D: processAluImm<AluOp::SBB, std::uint16_t>(); break;
  case 0x1E: pushSegment( mState.ds ); break;
  case 0x1F: popSegment( mState.ds ); break;

  case 0x20: processAluModRm<AluOp::AND, false, std::uint8_t>(); break;
  case 0x21: processAluModRm<AluOp::AND, false, std::uint16_t>(); break;
  case 0x22: processAluModRm<AluOp::AND, true, std::uint8_t>(); break;
  case 0x23: processAluModRm<AluOp::AND, true, std::uint16_t>(); break;
  case 0x24: processAluImm<AluOp::AND, std::uint8_t>(); break;
  case 0x25: processAluImm<AluOp::AND, std::uint16_t>(); break;
  case 0x27: daa(); break;
  case 0x28: processAluModRm<AluOp::SUB, false, std::uint8_t>(); break;
  case 0x29: processAluModRm<AluOp::SUB, false, std::uint16_t>(); break;
  case 0x2A: processAluModRm<AluOp::SUB, true, std::uint8_t>(); break;
  case 0x2B: processAluModRm<AluOp::SUB, true, std::uint16_t>(); break;
  case 0x2C: processAluImm<AluOp::SUB, std::uint8_t>(); break;
  case 0x2D: processAluImm<AluOp::SUB, std::uint16_t>(); break;
  case 0x2F: das(); break;

  case 0x30: processAluModRm<AluOp::XOR, false, std::uint8_t>(); break;
  case 0x31: processAluModRm<AluOp::XOR, false, std::uint16_t>(); break;
  case 0x32: processAluModRm<AluOp::XOR, true, std::uint8_t>(); break;
  case 0x33: processAluModRm<AluOp::XOR, true, std::uint16_t>(); break;
  case 0x34: processAluImm<AluOp::XOR, std::uint8_t>(); break;
  case 0x35: processAluImm<AluOp::XOR, std::uint16_t>(); break;
  case 0x37: aaa(); break;
  case 0x38: processAluModRm<AluOp::CMP, false, std::uint8_t>(); break;
  case 0x39: processAluModRm<AluOp::CMP, false, std::uint16_t>(); break;
  case 0x3A: processAluModRm<AluOp::CMP, true, std::uint8_t>(); break;
  case 0x3B: processAluModRm<AluOp::CMP, true, std::uint16_t>(); break;
  case 0x3C: processAluImm<AluOp::CMP, std::uint8_t>(); break;
  case 0x3D: processAluImm<AluOp::CMP, std::uint16_t>(); break;
  case 0x3F: aas(); break;

  case 0x40: inc( mState.ax ); break;
  case 0x41: inc( mState.cx ); break;
  case 0x42: inc( mState.dx ); break;
  case 0x43: inc( mState.bx ); break;
  case 0x44: inc( mState.sp ); break;
  case 0x45: inc( mState.bp ); break;
  case 0x46: inc( mState.si ); break;
  case 0x47: inc( mState.di ); break;
  case 0x48: dec( mState.ax ); break;
  case 0x49: dec( mState.cx ); break;
  case 0x4A: dec( mState.dx ); break;
  case 0x4B: dec( mState.bx ); break;
  case 0x4C: dec( mState.sp ); break;
  case 0x4D: dec( mState.bp ); break;
  case 0x4E: dec( mState.si ); break;
  case 0x4F: dec( mState.di ); break;

  case 0x50: push( mState.ax ); break;
  case 0x51: push( mState.cx ); break;
  case 0x52: push( mState.dx ); break;
  case 0x53: push( mState.bx ); break;
  case 0x54: pushSp(); break;
  case 0x55: push( mState.bp ); break;
  case 0x56: push( mState.si ); break;
  case 0x57: push( mState.di ); break;
  case 0x58: pop( mState.ax ); break;
  case 0x59: pop( mState.cx ); break;
  case 0x5A: pop( mState.dx ); break;
  case 0x5B: pop( mState.bx ); break;
  case 0x5C: pop( mState.sp ); break;
  case 0x5D: pop( mState.bp ); break;
  case 0x5E: pop( mState.si ); break;
  case 0x5F: pop( mState.di ); break;

  case 0x60: pushAll(); break;
  case 0x61: popAll(); break;
  case 0x62: bound(); break;
  case 0x63: undefined(); break;
  case 0x64: undefined(); break;
  case 0x65: undefined(); break;
  case 0x66: undefined(); break;
  case 0x67: undefined(); break;
  case 0x68: push( readCodeWord() ); break;
  case 0x69: multSignedModRm<std::uint16_t>(); break;
  case 0x6A: push( static_cast<std::uint16_t>( static_cast<std::int8_t>( readCodeByte() ) ) ); break;
  case 0x6B: multSignedModRm<std::uint8_t>(); break;
  case 0x6C: ins<std::uint8_t>(); break;
  case 0x6D: ins<std::uint16_t>(); break;
  case 0x6E: outs<std::uint8_t>(); break;
  case 0x6F: outs<std::uint16_t>(); break;

  case 0x70: jump( mState.flags.overflow ); break;
  case 0x71: jump( !mState.flags.overflow ); break;
  case 0x72: jump( mState.flags.carry ); break;
  case 0x73: jump( !mState.flags.carry ); break;
  case 0x74: jump( mState.flags.zero ); break;
  case 0x75: jump( !mState.flags.zero ); break;
  case 0x76: jump( mState.flags.carry || mState.flags.zero ); break;
  case 0x77: jump( !mState.flags.carry && !mState.flags.zero ); break;
  case 0x78: jump( mState.flags.sign ); break;
  case 0x79: jump( !mState.flags.sign ); break;
  case 0x7A: jump( mState.flags.parity ); break;
  case 0x7B: jump( !mState.flags.parity ); break;
  case 0x7C: jump( mState.flags.sign != mState.flags.overflow ); break;
  case 0x7D: jump( mState.flags.sign == mState.flags.overflow ); break;
  case 0x7E: jump( mState.flags.sign != mState.flags.overflow || mState.flags.zero ); break;
  case 0x7F: jump( mState.flags.sign == mState.flags.overflow && !mState.flags.zero ); break;

  case 0x80: grp1ModRm<false, std::uint8_t>(); break;
  case 0x81: grp1ModRm<false, std::uint16_t>(); break;
  case 0x82: grp1ModRm<true, std::uint8_t>(); break;
  case 0x83: grp1ModRm<true, std::uint16_t>(); break;
  case 0x84: testModRm<std::uint8_t>(); break;
  case 0x85: testModRm<std::uint16_t>(); break;
  case 0x86: exchangeModRm<std::uint8_t>(); break;
  case 0x87: exchangeModRm<std::uint16_t>(); break;
  case 0x88: moveModRm<false, std::uint8_t>(); break;
  case 0x89: moveModRm<false, std::uint16_t>(); break;
  case 0x8A: moveModRm<true, std::uint8_t>(); break;
  case 0x8B: moveModRm<true, std::uint16_t>(); break;
  case 0x8C: moveSegment<false>(); break;
  case 0x8D: lea(); break;
  case 0x8E: moveSegment<true>(); break;
  case 0x8F: popMemory(); break;

  case 0x90: nop(); break;
  case 0x91: exchange( mState.cx, mState.ax ); break;
  case 0x92: exchange( mState.dx, mState.ax ); break;
  case 0x93: exchange( mState.bx, mState.ax ); break;
  case 0x94: exchange( mState.sp, mState.ax ); break;
  case 0x95: exchange( mState.bp, mState.ax ); break;
  case 0x96: exchange( mState.si, mState.ax ); break;
  case 0x97: exchange( mState.di, mState.ax ); break;
  case 0x98: cbw(); break;
  case 0x99: cwd(); break;
  case 0x9A: callFar(); break;
  case 0x9B: wait(); break;
  case 0x9C: pushFlags(); break;
  case 0x9D: popFlags(); break;
  case 0x9E: sahf(); break;
  case 0x9F: lahf(); break;

  case 0xA0: moveAccumulator<false, std::uint8_t>(); break;
  case 0xA1: moveAccumulator<false, std::uint16_t>(); break;
  case 0xA2: moveAccumulator<true, std::uint8_t>(); break;
  case 0xA3: moveAccumulator<true, std::uint16_t>(); break;
  case 0xA4: movs<std::uint8_t>(); break;
  case 0xA5: movs<std::uint16_t>(); break;
  case 0xA6: cmps<std::uint8_t>(); break;
  case 0xA7: cmps<std::uint16_t>(); break;
  case 0xA8: testImmediate<std::uint8_t>(); break;
  case 0xA9: testImmediate<std::uint16_t>(); break;
  case 0xAA: stos<std::uint8_t>(); break;
  case 0xAB: stos<std::uint16_t>(); break;
  case 0xAC: lods<std::uint8_t>(); break;
  case 0xAD: lods<std::uint16_t>(); break;
  case 0xAE: scas<std::uint8_t>(); break;
  case 0xAF: scas<std::uint16_t>(); break;

  case 0xB0: moveLo( mState.ax, readCodeByte() ); break;
  case 0xB1: moveLo( mState.cx, readCodeByte() ); break;
  case 0xB2: moveLo( mState.dx, readCodeByte() ); break;
  case 0xB3: moveLo( mState.bx, readCodeByte() ); break;
  case 0xB4: moveHi( mState.ax, readCodeByte() ); break;
  case 0xB5: moveHi( mState.cx, readCodeByte() ); break;
  case 0xB6: moveHi( mState.dx, readCodeByte() ); break;
  case 0xB7: moveHi( mState.bx, readCodeByte() ); break;
  case 0xB8: move( mState.ax, readCodeWord() ); break;
  case 0xB9: move( mState.cx, readCodeWord() ); break;
  case 0xBA: move( mState.dx, readCodeWord() ); break;
  case 0xBB: move( mState.bx, readCodeWord() ); break;
  case 0xBC: move( mState.sp, readCodeWord() ); break;
  case 0xBD: move( mState.bp, readCodeWord() ); break;
  case 0xBE: move( mState.si, readCodeWord() ); break;
  case 0xBF: move( mState.di, readCodeWord() ); break;

  case 0xC0: grp2ModRm<std::uint8_t, Grp2Mode::IMMEDIATE>(); break;
  case 0xC1: grp2ModRm<std::uint16_t, Grp2Mode::IMMEDIATE>(); break;
  case 0xC2: retImm(); break;
  case 0xC3: ret(); break;
  case 0xC4: les(); break;
  case 0xC5: lds(); break;
  case 0xC6: moveImmediate<std::uint8_t>(); break;
  case 0xC7: moveImmediate<std::uint16_t>(); break;
  case 0xC8: enter(); break;
  case 0xC9: leave(); break;
  case 0xCA: retFarImm(); break;
  case 0xCB: retFar(); break;
  case 0xCC: idle( 5 ); interrupt( 3 ); break;
  case 0xCD: idle( 6 ); interrupt( readCodeByte() ); break;
  case 0xCE: interruptOverflow(); break;
  case 0xCF: retInterrupt(); break;

  case 0xD0: grp2ModRm<std::uint8_t, Grp2Mode::ONE>(); break;
  case 0xD1: grp2ModRm<std::uint16_t, Grp2Mode::ONE>(); break;
  case 0xD2: grp2ModRm<std::uint8_t, Grp2Mode::CL>(); break;
  case 0xD3: grp2ModRm<std::uint16_t, Grp2Mode::CL>(); break;
  case 0xD4: aam(); break;
  case 0xD5: aad(); break;
  case 0xD6: salc(); break;
  case 0xD7: xlat(); break;
  case 0xD8: fp01(); break;
  case 0xD9: fp01(); break;
  case 0xDA: fp01(); break;
  case 0xDB: fp01(); break;
  case 0xDC: fp01(); break;
  case 0xDD: fp01(); break;
  case 0xDE: fp01(); break;
  case 0xDF: fp01(); break;

  case 0xE0: loopIf( !mState.flags.zero ); break;
  case 0xE1: loopIf( mState.flags.zero ); break;
  case 0xE2: loop(); break;
  case 0xE3: jump( mState.cx == 0 ); break;
  case 0xE4: inStoreAx<std::uint8_t, false>( readCodeByte() ); break;
  case 0xE5: inStoreAx<std::uint16_t, false>( readCodeByte() ); break;
  case 0xE6: out<std::uint8_t>( 6, readCodeByte(), static_cast<std::uint8_t>( mState.ax ) ); break;
  case 0xE7: out<std::uint16_t>( 6, readCodeByte(), mState.ax ); break;
  case 0xE8: callNearWord(); break;
  case 0xE9: jumpNearWord(); break;
  case 0xEA: jumpFar(); break;
  case 0xEB: jump( true ); break;
  case 0xEC: inStoreAx<std::uint8_t, true>( mState.dx ); break;
  case 0xED: inStoreAx<std::uint16_t, true>( mState.dx ); break;
  case 0xEE: out<std::uint8_t>( 4, mState.dx, static_cast<std::uint8_t>( mState.ax ) ); break;
  case 0xEF: out<std::uint16_t>( 4, mState.dx, mState.ax ); break;

  case 0xF1: undefined(); break;
  case 0xF4: halt(); break;
  case 0xF5: setFlagValue( mState.flags.carry, !mState.flags.carry ); break; // CMC
  case 0xF6: grp3ModRm<std::uint8_t>(); break;
  case 0xF7: grp3ModRm<std::uint16_t>(); break;
  case 0xF8: setFlagValue( mState.flags.carry, false ); break;     // CLC
  case 0xF9: setFlagValue( mState.flags.carry, true ); break;      // STC
  case 0xFA: setFlagValue( mState.flags.irq, false ); break;       // CLI
  case 0xFB: setIrqFlag(); break;                                  // STI
  case 0xFC: setFlagValue( mState.flags.direction, false ); break; // CLD
  case 0xFD: setFlagValue( mState.flags.direction, true ); break;  // STD
  case 0xFE: grp45ModRm<std::uint8_t>(); break;
  case 0xFF: grp45ModRm<std::uint16_t>(); break;

  default:
    // The prefixes, taken by processPrefix().
    break;
  }
  // NOLINTEND(bugprone-branch-clone)
  // clang-format on

  if ( !mPrefix.preserve )
  {
    mPrefix = {};
  }

  if ( mState.flags.trap && mSuppressTrapClock != mState.cycleCount )
  {
    interrupt( 1 );
  }
}

} // namespace ws::machine
