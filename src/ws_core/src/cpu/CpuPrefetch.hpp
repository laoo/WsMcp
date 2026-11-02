#pragma once

// The V30MZ's prefetch queue. Ported from Mesen2's Core/WS/WsCpuPrefetch.h and
// WsCpuPrefetch.cpp at b9fa69ddc6d0a331fb103fdb5eef6904305703c2.

#include <array>
#include <cstdint>

namespace ws::machine
{

class Cpu;
class MemoryManager;
class System;

class CpuPrefetch
{
public:
  /// The memory manager is taken from `system` on first use: the System
  /// builds the CPU before its memory manager.
  explicit CpuPrefetch( Cpu& cpu, System& system );

  void prefetch();
  void processRep( std::uint8_t opCode );
  std::uint8_t read();
  void clear( std::uint16_t cs, std::uint16_t ip );

  template <class Archive>
  void serialize( Archive& archive )
  {
    archive( mFetchCs );
    archive( mFetchIp );
    archive( mReadPos );
    archive( mWritePos );
    archive( mSize );
    archive( mWaitCycles );
    archive( mData );
  }

private:
  [[nodiscard]] bool isFull() const;
  void pushByte( std::uint8_t value );
  MemoryManager& mm();

  std::uint16_t mFetchCs{};
  std::uint16_t mFetchIp{};

  std::uint8_t mReadPos{};
  std::uint8_t mWritePos{};
  std::uint8_t mSize{};

  std::uint8_t mWaitCycles{};

  std::array<std::uint8_t, 16> mData{};

  Cpu* mCpu;
  System* mSystem;
  MemoryManager* mMemoryManager{};
};

} // namespace ws::machine
