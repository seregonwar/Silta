#pragma once
// Colors in sRGB with straight alpha, like CSS. Tokens are defined in OKLCH
// (the space shadcn/ui and Tailwind v4 use) and converted once, so themes copy
// over from the web exactly.
#include <cstdint>
#include "imgui.h"

namespace silta {

struct Color {
  float r = 0, g = 0, b = 0, a = 1;

  constexpr Color() = default;
  constexpr Color(float r_, float g_, float b_, float a_ = 1) : r(r_), g(g_), b(b_), a(a_) {}

  // 0xRRGGBB
  static Color hex(uint32_t rgb, float alpha = 1);
  // CSS oklch(L C H / alpha): L in [0, 1], C chroma, H hue in degrees.
  static Color oklch(float l, float c, float h, float alpha = 1);

  // Same color, alpha replaced.
  constexpr Color with_alpha(float alpha) const { return {r, g, b, alpha}; }
  // Same color, alpha multiplied: Tailwind's `bg-primary/90` is primary.fade(.9f).
  constexpr Color fade(float factor) const { return {r, g, b, a * factor}; }

  ImVec4 vec4() const { return {r, g, b, a}; }
  // Packed for ImDrawList. Applies ImGui's style alpha, so disabled scopes fade.
  ImU32 u32() const;

  constexpr bool operator==(const Color &) const = default;
};

// Linear interpolation of every channel, t in [0, 1].
Color mix(Color a, Color b, float t);
// `top` composited over `bottom` (CSS source-over).
Color over(Color top, Color bottom);

inline constexpr Color white{1, 1, 1, 1};
inline constexpr Color black{0, 0, 0, 1};
inline constexpr Color transparent{0, 0, 0, 0};

} // namespace silta
