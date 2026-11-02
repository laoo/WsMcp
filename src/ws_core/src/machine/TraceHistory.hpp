#pragma once

// The history of execution, as Machine keeps it: every instruction begun, in
// a ring of blocks of 65536. A block every instruction in which has been let
// go is kept for the next ones rather than given back, so that a history of
// steady length allocates nothing once it is full.

#include "ws/machine/Machine.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>

namespace ws::machine
{

class TraceHistory
{
public:
  /// Notes an instruction, after the last.
  void push( TracedInstruction const& instruction );
  /// Lets go of the instructions begun before cycle `cutoff`, oldest first.
  void forgetBefore( std::uint64_t cutoff );

  [[nodiscard]] std::size_t size() const;
  /// The `index`th kept, 0 being the oldest.
  [[nodiscard]] TracedInstruction const& at( std::size_t index ) const;

private:
  static constexpr std::size_t BLOCK = 65536;
  using Block = std::array<TracedInstruction, BLOCK>;

  std::deque<std::unique_ptr<Block>> mBlocks;
  /// A block let go, kept for the next.
  std::unique_ptr<Block> mSpare;
  /// The oldest kept's place in the first block, and how many are kept.
  std::size_t mFirst{};
  std::size_t mSize{};
};

} // namespace ws::machine
