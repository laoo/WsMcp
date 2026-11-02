// The PPU's 12-bit pixels to RGBA8888, through a table of the 4096 colours.
// Ported from Mesen2's Core/WS/WsDefaultVideoFilter.cpp at
// b9fa69ddc6d0a331fb103fdb5eef6904305703c2.

#include "Picture.hpp"

#include <cassert>
#include <cmath>
#include <cstddef>

namespace ws::machine
{

namespace
{

using Palette = std::array<std::array<std::uint8_t, 4>, 0x1000>;

Palette makePalette( bool monochrome )
{
  Palette palette{};
  for ( std::uint32_t rgb444 = 0; rgb444 < 0x1000; rgb444++ )
  {
    std::uint32_t r = ( rgb444 >> 8U ) & 0xFU;
    std::uint32_t g = ( rgb444 >> 4U ) & 0xFU;
    std::uint32_t b = rgb444 & 0xFU;

    if ( monochrome )
    {
      // Same formula as what asie recently implemented for ares
      auto const applyGamma = []( std::uint32_t color, std::uint32_t min, std::uint32_t max )
      {
        return min + static_cast<std::uint32_t>( std::pow( color / 15.0, 1 / 2.2 ) * static_cast<double>( max - min ) );
      };

      r = applyGamma( r, 0x2B, 0xE0 );
      g = applyGamma( g, 0x2B, 0xDB );
      b = applyGamma( b, 0x26, 0xCD );
    }
    else
    {
      std::uint32_t const r2 = ( r * 13 ) + ( g * 2 ) + b;
      std::uint32_t const g2 = ( g * 12 ) + ( b * 4 );
      std::uint32_t const b2 = ( r * 3 ) + ( g * 2 ) + ( b * 11 );
      r = r2;
      g = g2;
      b = b2;
    }

    palette[rgb444] = {
      static_cast<std::uint8_t>( r ), static_cast<std::uint8_t>( g ), static_cast<std::uint8_t>( b ), 0xFF
    };
  }
  return palette;
}

Palette const& paletteOf( Model model )
{
  static Palette const MONOCHROME_PALETTE = makePalette( true );
  static Palette const COLOR_PALETTE = makePalette( false );
  return model == Model::MONOCHROME ? MONOCHROME_PALETTE : COLOR_PALETTE;
}

} // namespace

std::array<std::uint8_t, 4> toRgba( std::uint16_t pixel, Model model )
{
  return paletteOf( model )[pixel & 0xFFFU];
}

void toRgba( std::span<std::uint16_t const> pixels, Model model, std::span<std::uint8_t> rgba )
{
  assert( rgba.size() >= pixels.size() * 4 );
  Palette const& palette = paletteOf( model );
  for ( std::size_t i = 0; i < pixels.size(); i++ )
  {
    std::array<std::uint8_t, 4> const& colour = palette[pixels[i] & 0xFFFU];
    rgba[i * 4] = colour[0];
    rgba[( i * 4 ) + 1] = colour[1];
    rgba[( i * 4 ) + 2] = colour[2];
    rgba[( i * 4 ) + 3] = colour[3];
  }
}

} // namespace ws::machine
