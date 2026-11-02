#pragma once

// The NEC V30MZ. Ported from Mesen2's Core/WS/WsCpu.h and WsCpu.cpp at
// b9fa69ddc6d0a331fb103fdb5eef6904305703c2.

#include "cpu/CpuPrefetch.hpp"
#include "machine/System.hpp"

#include <array>
#include <cstdint>

namespace ws::machine
{

class MemoryManager;

class Cpu
{
public:
  explicit Cpu( System& system );

  // The register tables point into the object itself.
  Cpu( Cpu const& ) = delete;
  Cpu& operator=( Cpu const& ) = delete;
  Cpu( Cpu&& ) = delete;
  Cpu& operator=( Cpu&& ) = delete;
  ~Cpu() = default;

  CpuState& getState()
  {
    return mState;
  }

  [[nodiscard]] CpuState const& getState() const
  {
    return mState;
  }

  [[nodiscard]] std::uint64_t getCycleCount() const
  {
    return mState.cycleCount;
  }

  [[nodiscard]] bool isPowerOff() const
  {
    return mState.powerOff;
  }

  /// A cycle of the bus with no access: the clock, a PPU dot, an APU clock.
  void processCpuCycle();

  void incCycleCount()
  {
    mState.cycleCount++;
  }

  void clearPrefetch();

  /// The linear address of the instruction at CS:IP; with
  /// `adjustForRepLoop`, of its first prefix while a REP repeats it.
  [[nodiscard]] std::uint32_t getProgramCounter( bool adjustForRepLoop = false ) const;

  /// Mesen2's Exec, in two halves so that the machine can stop between them,
  /// where Mesen2 calls its debugger's ProcessInstruction: the halt, and the
  /// interrupt taken before the instruction, then the instruction.
  /// prepareInstruction() answers false where the CPU is halted and spent a
  /// cycle idle instead; then there is no instruction to run.
  bool prepareInstruction();
  void execOpCode();

  void exec()
  {
    if ( prepareInstruction() )
    {
      execOpCode();
    }
  }

  /// Whether the next execOpCode() runs another iteration of a string
  /// instruction under REP, rather than an instruction of its own.
  [[nodiscard]] bool isRepeating() const;

  template <class Archive>
  void serialize( Archive& archive )
  {
    archive( mState.cycleCount );
    archive( mState.cs );
    archive( mState.ip );
    archive( mState.ss );
    archive( mState.sp );
    archive( mState.bp );
    archive( mState.ds );
    archive( mState.es );
    archive( mState.si );
    archive( mState.di );
    archive( mState.ax );
    archive( mState.bx );
    archive( mState.cx );
    archive( mState.dx );
    archive( mState.halted );
    archive( mState.powerOff );
    archive( mState.flags );

    archive( mOpCode );
    archive( mMulOverflow );
    mPrefetch.serialize( archive );
    archive( mPrefix.lock );
    archive( mPrefix.prefixCount );
    archive( mPrefix.preserve );
    archive( mPrefix.rep );
    archive( mPrefix.segment );

    archive( mModRm.segment );
    archive( mModRm.offset );
    archive( mModRm.mode );
    archive( mModRm.reg );
    archive( mModRm.rm );

    archive( mSuppressIrqClock );
    archive( mSuppressTrapClock );
  }

private:
  struct ModRmState
  {
    std::uint16_t segment{};
    std::uint16_t offset{};
    std::uint8_t mode{};
    // Mesen2's Register; `register` is a keyword.
    std::uint8_t reg{};
    std::uint8_t rm{};
  };

  enum class RepMode : std::uint8_t
  {
    NONE,
    ZERO,
    NOT_ZERO
  };

  enum class AluOp : std::uint8_t
  {
    ADD = 0,
    OR,
    ADC,
    SBB,
    AND,
    SUB,
    XOR,
    CMP
  };

  enum class Grp2Mode : std::uint8_t
  {
    ONE,
    CL,
    IMMEDIATE
  };

  struct PrefixState
  {
    std::uint16_t prefixCount{};
    Segment segment{};
    RepMode rep{};
    bool lock{};
    bool preserve{};
  };

  MemoryManager& mm();

  template <typename T>
  T readPort( std::uint16_t port );
  template <typename T>
  void writePort( std::uint16_t port, T value );

  std::uint8_t readCodeByte();
  std::uint16_t readCodeWord();
  template <typename T>
  T readImmediate();

  template <typename T>
  T readMemory( std::uint16_t seg, std::uint16_t offset );
  template <typename T>
  void writeMemory( std::uint16_t seg, std::uint16_t offset, T value );

  void idle( std::uint8_t cycles = 1 );

  void move( std::uint16_t& dst, std::uint16_t src );
  void moveLo( std::uint16_t& dst, std::uint8_t src );
  void moveHi( std::uint16_t& dst, std::uint8_t src );

  void push( std::uint16_t value );
  void pushSp();
  void pop( std::uint16_t& dst );
  void pushSegment( std::uint16_t value );
  void popSegment( std::uint16_t& dst );
  void pushFlags();
  void popFlags();
  void setFlags( std::uint16_t flags );
  void popMemory();
  void popAll();
  void pushAll();

  std::uint16_t getSegment( Segment defaultSegment );

  void readModRmByte();

  template <typename T>
  T getModRegister( std::uint8_t reg );
  template <typename T>
  void setModRegister( std::uint8_t reg, T value );

  std::uint16_t getModSegRegister( std::uint8_t reg );
  void setModSegRegister( std::uint8_t reg, std::uint16_t value );

  template <typename T>
  T getModRm();
  template <typename T>
  void setModRm( T value );

  template <bool SIGN, typename T>
  void grp1ModRm();
  template <typename T, Grp2Mode MODE>
  void grp2ModRm();
  template <typename T>
  void grp3ModRm();
  template <typename T>
  void grp45ModRm();

  template <typename T>
  void testModRm();
  template <typename T>
  void testImmediate();

  template <typename T>
  void exchangeModRm();
  void exchange( std::uint16_t& x, std::uint16_t& y );

  template <bool DIRECTION, typename T>
  void moveModRm();
  template <bool DIRECTION>
  void moveSegment();
  template <bool DIRECTION, typename T>
  void moveAccumulator();
  template <typename T>
  void moveImmediate();

  template <AluOp OP, bool DIRECTION, typename T>
  void processAluModRm();
  template <AluOp OP, typename T>
  void processAluImm();
  template <typename T>
  T getAluResult( AluOp op, T param1, T param2 );

  template <typename T>
  void inc( T& dst );
  template <typename T>
  void dec( T& dst );

  template <typename T>
  T add( T x, T y, std::uint8_t carry );
  template <typename T>
  T sub( T x, T y, std::uint8_t borrow );

  template <typename T>
  void multSignedModRm();
  template <typename T>
  void mulSigned( T x, T y );
  template <typename T>
  std::int32_t getMultiplyResult( T x, T y );
  template <typename T>
  void mulUnsigned( T x, T y );

  template <typename T>
  void divSigned( T y );
  template <typename T>
  void divUnsigned( T y );
  void processInvalidDiv();

  template <typename T>
  void updateFlags( T result );
  template <typename T>
  T bitAnd( T x, T y );
  template <typename T>
  T bitOr( T x, T y );
  template <typename T>
  T bitXor( T x, T y );
  template <typename T>
  T rol( T x, std::uint8_t shift );
  template <typename T>
  T ror( T x, std::uint8_t shift );
  template <typename T>
  T rcl( T x, std::uint8_t shift );
  template <typename T>
  T rcr( T x, std::uint8_t shift );
  template <typename T>
  T shl( T x, std::uint8_t shift );
  template <typename T>
  T shr( T x, std::uint8_t shift );
  template <typename T>
  T sar( T x, std::uint8_t shift );

  void jumpFar();
  void jump( bool condition );
  void jumpNearWord();
  void call( std::uint16_t offset );
  void callNearWord();
  void callFar();
  void callFar( std::uint16_t segment, std::uint16_t offset );
  void loop();
  void loopIf( bool condition );
  void ret();
  void retImm();
  void retFar();
  void retFarImm();
  void retInterrupt();

  void interrupt( std::uint8_t vector, bool pushFirstPrefix = false );
  void interruptOverflow();

  void enter();
  void leave();

  void nop();
  void fp01();

  void bound();

  void salc();

  void cbw();
  void cwd();

  void lahf();
  void sahf();

  void ldsLesLeaModRm();

  std::uint16_t loadSegment();
  void lds();
  void les();
  void lea();

  void xlat();

  void adjustAscii( bool forSub );
  void aaa();
  void aas();

  void aad();
  void aam();

  void adjustDecimal( bool forSub );
  void daa();
  void das();

  template <typename T>
  void out( std::uint8_t delay, std::uint16_t port, T data );
  template <typename T, bool IS_DX_PORT>
  void inStoreAx( std::uint16_t port );

  template <typename T>
  void ins();
  template <typename T>
  void outs();

  template <typename T>
  [[nodiscard]] std::uint16_t getStringStep() const;
  template <typename T>
  void processStringOperation( bool incSi, bool incDi );
  template <typename T>
  void processStringCmpOperation( bool incSi );
  template <typename T>
  void movs();
  template <typename T>
  void stos();
  template <typename T>
  void lods();
  template <typename T>
  void cmps();
  template <typename T>
  void scas();

  void undefined();
  void wait();
  void halt();

  void suppressIrq( bool suppressTrap );

  void setFlagValue( bool& flag, bool value );
  void setIrqFlag();

  /// Mesen2's ExecOpCode keeps the prefixes in its switch, reached by goto;
  /// here a prefix is taken by this, before the switch.
  bool processPrefix( std::uint8_t opCode );

  System* mSystem;
  MemoryManager* mMemoryManager{};
  CpuState mState{};
  ModRmState mModRm{};
  PrefixState mPrefix{};

  std::uint64_t mSuppressIrqClock{};
  std::uint64_t mSuppressTrapClock{};

  CpuPrefetch mPrefetch;

  std::array<std::uint16_t*, 4> mModRegLut8{};
  std::array<std::uint16_t*, 4> mModSegLut16{};
  std::array<std::uint16_t*, 8> mModRegLut16{};

  // Used to re-fill prefetch buffer with last opcode when REP prefix is used
  std::uint8_t mOpCode{};

  // Divisions/AAM set carry/overflow to the last carry/overflow produced by the previous MUL operation
  bool mMulOverflow{};
};

} // namespace ws::machine
