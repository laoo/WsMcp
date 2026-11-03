#include "TraceHistory.hpp"

#include <algorithm>

namespace ws::machine
{

void TraceHistory::push( TracedInstruction const& instruction )
{
  std::size_t const end = mFirst + mSize;
  if ( end == mBlocks.size() * BLOCK )
  {
    mBlocks.push_back( mSpare ? std::move( mSpare ) : std::make_unique<Block>() );
  }
  ( *mBlocks[end / BLOCK] )[end % BLOCK] = instruction;
  ++mSize;
}

void TraceHistory::forgetBefore( std::uint64_t cutoff )
{
  // The instructions are kept in the order they began, so the first block's
  // are searched for the cutoff rather than let go one by one.
  while ( mSize > 0 )
  {
    Block const& first = *mBlocks.front();
    auto const begin = first.begin() + static_cast<std::ptrdiff_t>( mFirst );
    auto const end = first.begin() + static_cast<std::ptrdiff_t>( std::min( BLOCK, mFirst + mSize ) );
    auto const kept = std::partition_point(
        begin, end, [cutoff]( TracedInstruction const& instruction ) { return instruction.cycle < cutoff; } );
    auto const forgotten = static_cast<std::size_t>( kept - begin );
    mFirst += forgotten;
    mSize -= forgotten;
    if ( mFirst != BLOCK )
    {
      return;
    }
    mSpare = std::move( mBlocks.front() );
    mBlocks.pop_front();
    mFirst = 0;
  }
}

std::size_t TraceHistory::size() const
{
  return mSize;
}

TracedInstruction const& TraceHistory::at( std::size_t index ) const
{
  std::size_t const place = mFirst + index;
  return ( *mBlocks.at( place / BLOCK ) ).at( place % BLOCK );
}

} // namespace ws::machine
