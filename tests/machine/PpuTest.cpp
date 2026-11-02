#include <catch2/catch_test_macros.hpp>

#include "machine/Picture.hpp"

#include <array>
#include <cstdint>
#include <vector>

// The PPU itself needs a whole System; its picture's colours do not.

using ws::machine::Model;
using ws::machine::toRgba;

TEST_CASE( "the Color's panel mixes the components", "[ppu]" )
{
  using Rgba = std::array<std::uint8_t, 4>;
  REQUIRE( toRgba( 0x000, Model::COLOR ) == Rgba{ 0, 0, 0, 0xFF } );
  REQUIRE( toRgba( 0xFFF, Model::COLOR ) == Rgba{ 240, 240, 240, 0xFF } );
  REQUIRE( toRgba( 0xF00, Model::COLOR ) == Rgba{ 195, 0, 45, 0xFF } );
  REQUIRE( toRgba( 0x0F0, Model::COLOR ) == Rgba{ 30, 180, 30, 0xFF } );
  REQUIRE( toRgba( 0x00F, Model::COLOR ) == Rgba{ 15, 60, 165, 0xFF } );
  REQUIRE( toRgba( 0x123, Model::SWAN_CRYSTAL ) == toRgba( 0x123, Model::COLOR ) );
}

TEST_CASE( "the WonderSwan's panel spans its grey-green range", "[ppu]" )
{
  using Rgba = std::array<std::uint8_t, 4>;
  REQUIRE( toRgba( 0x000, Model::MONOCHROME ) == Rgba{ 0x2B, 0x2B, 0x26, 0xFF } );
  REQUIRE( toRgba( 0xFFF, Model::MONOCHROME ) == Rgba{ 0xE0, 0xDB, 0xCD, 0xFF } );

  // The gamma lifts the middle shades above the range's midpoint.
  REQUIRE( toRgba( 0x888, Model::MONOCHROME )[0] > ( 0x2B + 0xE0 ) / 2 );
  for ( std::uint16_t shade = 1; shade < 16; ++shade )
  {
    auto const lighter = static_cast<std::uint16_t>( shade * 0x111 );
    auto const darker = static_cast<std::uint16_t>( ( shade - 1 ) * 0x111 );
    REQUIRE( toRgba( lighter, Model::MONOCHROME )[0] > toRgba( darker, Model::MONOCHROME )[0] );
  }
}

TEST_CASE( "a picture becomes four bytes a pixel, in order", "[ppu]" )
{
  std::vector<std::uint16_t> const pixels{ 0xFFF, 0xF00, 0x000 };
  std::vector<std::uint8_t> rgba( pixels.size() * 4 );
  toRgba( pixels, Model::COLOR, rgba );
  REQUIRE( rgba == std::vector<std::uint8_t>{ 240, 240, 240, 0xFF, 195, 0, 45, 0xFF, 0, 0, 0, 0xFF } );
}
