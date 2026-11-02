#pragma once

// The two directions of a save state, as PGMEmu has them. Each part of the
// machine names its state once, in a serialize() that takes either archive:
// the writer copies each field out, the reader copies it back in, in the same
// order. Fields are copied as their bytes, so only trivially copyable ones are
// taken. Each part writes a chunk of its own, tagged with a FOURCC and its
// layout's version, with its size, so that a part's layout can change without
// the others' (docs/architecture.md, "Save states").

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>
#include <type_traits>
#include <vector>

namespace ws::machine
{

/// A chunk's tag: four characters.
using FourCc = std::array<char, 4>;

constexpr FourCc fourCc( std::string_view text )
{
  return { text[0], text[1], text[2], text[3] };
}

class StateWriter
{
public:
  static constexpr bool READING = false;

  template <class T>
    requires std::is_trivially_copyable_v<T>
  void operator()( T const& value )
  {
    std::size_t const at = mBytes.size();
    mBytes.resize( at + sizeof( T ) );
    std::memcpy( mBytes.data() + at, &value, sizeof( T ) );
  }

  template <class T>
    requires std::is_trivially_copyable_v<T>
  void operator()( std::vector<T> const& values )
  {
    ( *this )( static_cast<std::uint64_t>( values.size() ) );
    std::size_t const at = mBytes.size();
    mBytes.resize( at + ( values.size() * sizeof( T ) ) );
    if ( !values.empty() )
    {
      std::memcpy( mBytes.data() + at, values.data(), values.size() * sizeof( T ) );
    }
  }

  /// Writes what `part` serializes as a chunk tagged `tag`, of layout
  /// `version`.
  template <class Part>
  void chunk( FourCc tag, std::uint16_t version, Part& part )
  {
    ( *this )( tag );
    ( *this )( version );
    std::size_t const sizeAt = mBytes.size();
    ( *this )( std::uint32_t{} );
    part.serialize( *this );
    auto const size = static_cast<std::uint32_t>( mBytes.size() - sizeAt - sizeof( std::uint32_t ) );
    std::memcpy( mBytes.data() + sizeAt, &size, sizeof( size ) );
  }

  [[nodiscard]] std::vector<std::uint8_t> const& bytes() const
  {
    return mBytes;
  }

private:
  std::vector<std::uint8_t> mBytes;
};

class StateReader
{
public:
  static constexpr bool READING = true;

  explicit StateReader( std::span<std::uint8_t const> bytes ) : mBytes{ bytes } {}

  template <class T>
    requires std::is_trivially_copyable_v<T>
  void operator()( T& value )
  {
    if ( take( sizeof( T ) ) )
    {
      std::memcpy( &value, mBytes.data() + mAt - sizeof( T ), sizeof( T ) );
    }
  }

  template <class T>
    requires std::is_trivially_copyable_v<T>
  void operator()( std::vector<T>& values )
  {
    std::uint64_t size = 0;
    ( *this )( size );
    if ( !mGood || size > ( mBytes.size() - mAt ) / sizeof( T ) )
    {
      mGood = false;
      return;
    }
    values.resize( static_cast<std::size_t>( size ) );
    if ( take( values.size() * sizeof( T ) ) && !values.empty() )
    {
      std::memcpy( values.data(), mBytes.data() + mAt - ( values.size() * sizeof( T ) ), values.size() * sizeof( T ) );
    }
  }

  /// Reads the chunk tagged `tag` into `part`, which must come next, be of
  /// layout `version`, and be read whole.
  template <class Part>
  void chunk( FourCc tag, std::uint16_t version, Part& part )
  {
    FourCc foundTag{};
    std::uint16_t foundVersion = 0;
    std::uint32_t size = 0;
    ( *this )( foundTag );
    ( *this )( foundVersion );
    ( *this )( size );
    if ( !mGood || foundTag != tag || foundVersion != version || size > mBytes.size() - mAt )
    {
      mGood = false;
      return;
    }
    StateReader inner{ mBytes.subspan( mAt, size ) };
    part.serialize( inner );
    mGood = inner.good() && inner.atEnd();
    mAt += size;
  }

  /// Marks the state as not this machine's: what was read does not fit it.
  void fail()
  {
    mGood = false;
  }

  /// Whether every field read was there.
  [[nodiscard]] bool good() const
  {
    return mGood;
  }

  /// Whether every byte was read.
  [[nodiscard]] bool atEnd() const
  {
    return mAt == mBytes.size();
  }

private:
  bool take( std::size_t size )
  {
    if ( !mGood || size > mBytes.size() - mAt )
    {
      mGood = false;
      return false;
    }
    mAt += size;
    return true;
  }

  std::span<std::uint8_t const> mBytes;
  std::size_t mAt{};
  bool mGood{ true };
};

} // namespace ws::machine
