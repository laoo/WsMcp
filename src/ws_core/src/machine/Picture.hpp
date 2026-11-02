#pragma once

// The PPU's pixels as the screen shows them. Ported from Mesen2's
// Core/WS/WsDefaultVideoFilter.h and WsDefaultVideoFilter.cpp at
// b9fa69ddc6d0a331fb103fdb5eef6904305703c2.

#include "ws/machine/Types.hpp"

#include <array>
#include <cstdint>
#include <span>

namespace ws::machine
{

// Each pixel is 0x0RGB, 4 bits a component, and becomes 4 bytes, R, G, B and
// an opaque A, as the LCD's colours look with Mesen2's defaults (LCD colour
// adjustment on, hue, saturation, brightness and contrast untouched): on the
// WonderSwan, each component goes through a 2.2 gamma onto the grey-green
// range of its panel (asie's formula for ares); on the Color and the
// SwanCrystal, the components are mixed to imitate the panel's colours
// (R' = 13R + 2G + B, G' = 12G + 4B, B' = 3R + 2G + 11B).

/// One pixel's R, G, B and A.
[[nodiscard]] std::array<std::uint8_t, 4> toRgba( std::uint16_t pixel, Model model );

/// `pixels` into `rgba`, which holds 4 bytes for each of them.
void toRgba( std::span<std::uint16_t const> pixels, Model model, std::span<std::uint8_t> rgba );

} // namespace ws::machine
