#include "TraceHistory.hpp"

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
  while ( mSize > 0 && at( 0 ).cycle < cutoff )
  {
    ++mFirst;
    --mSize;
    if ( mFirst == BLOCK )
    {
      mSpare = std::move( mBlocks.front() );
      mBlocks.pop_front();
      mFirst = 0;
    }
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
