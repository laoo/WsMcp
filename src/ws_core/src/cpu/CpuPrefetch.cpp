// The V30MZ's prefetch queue. Ported from Mesen2's Core/WS/WsCpuPrefetch.h and
// WsCpuPrefetch.cpp at b9fa69ddc6d0a331fb103fdb5eef6904305703c2.

#include "cpu/CpuPrefetch.hpp"

#include "cpu/Cpu.hpp"
#include "machine/MemoryManager.hpp"
#include "machine/System.hpp"

namespace ws::machine
{

CpuPrefetch::CpuPrefetch( Cpu& cpu, System& system ) : mCpu{ &cpu }, mSystem{ &system } {}

MemoryManager& CpuPrefetch::mm()
{
  if ( mMemoryManager == nullptr )
  {
    mMemoryManager = mSystem->getMemoryManager();
  }
  return *mMemoryManager;
}

bool CpuPrefetch::isFull() const
{
  return mSize >= 16;
}

void CpuPrefetch::pushByte( std::uint8_t value )
{
  mData[( mWritePos++ ) & 0x0FU] = value;
  mFetchIp++;
  mSize++;
}

void CpuPrefetch::prefetch()
{
  mCpu->processCpuCycle();

  mWaitCycles++;

  if ( isFull() )
  {
    return;
  }

  std::uint32_t const addr = ( ( static_cast<std::uint32_t>( mFetchCs ) << 4U ) + mFetchIp ) & 0xFFFFFU;
  std::uint8_t const cycles = mm().getWaitStates( addr );
  if ( mWaitCycles < cycles )
  {
    return;
  }

  mWaitCycles = 0;

  bool const isWordBus = mm().isWordBus( addr );

  // TODOWS debugger can't see these reads
  pushByte( mm().internalRead( addr ) );
  if ( !isFull() && isWordBus && ( mFetchIp & 0x01U ) != 0 )
  {
    // Mesen2 masks the byte read instead of the address; the address is masked here.
    pushByte( mm().internalRead( ( ( static_cast<std::uint32_t>( mFetchCs ) << 4U ) + mFetchIp ) & 0xFFFFFU ) );
  }
}

void CpuPrefetch::processRep( std::uint8_t opCode )
{
  if ( isFull() )
  {
    mWritePos--;
    mFetchIp--;
    mSize--;
  }

  mReadPos--;
  mSize++;
  mData[mReadPos & 0x0FU] = opCode;
}

std::uint8_t CpuPrefetch::read()
{
  while ( mSize < 2 )
  {
    prefetch();
  }

  mSize--;
  return mData[( mReadPos++ ) & 0x0FU];
}

void CpuPrefetch::clear( std::uint16_t cs, std::uint16_t ip )
{
  mFetchCs = cs;
  mFetchIp = ip;
  mSize = 0;
  mReadPos = 0;
  mWritePos = 0;
  mWaitCycles = 0;
}

} // namespace ws::machine
