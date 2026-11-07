#include "silta/color.hpp"
#include <algorithm>
#include <cmath>

namespace silta {
namespace {
float to_srgb(float linear) {
  linear = std::clamp(linear, 0.f, 1.f);
  return linear <= 0.0031308f ? 12.92f * linear : 1.055f * std::pow(linear, 1.f / 2.4f) - 0.055f;
}
} // namespace

Color Color::hex(uint32_t rgb, float alpha) {
  return {float((rgb >> 16) & 0xff) / 255.f, float((rgb >> 8) & 0xff) / 255.f, float(rgb & 0xff) / 255.f,
          alpha};
}

Color Color::oklch(float l, float c, float h, float alpha) {
  // OKLCH -> OKLab -> linear sRGB -> sRGB (Björn Ottosson's reference matrices).
  const float hr = h * 3.14159265358979f / 180.f;
  const float a = c * std::cos(hr), b = c * std::sin(hr);
  const float l_ = l + 0.3963377774f * a + 0.2158037573f * b;
  const float m_ = l - 0.1055613458f * a - 0.0638541728f * b;
  const float s_ = l - 0.0894841775f * a - 1.2914855480f * b;
  const float L = l_ * l_ * l_, M = m_ * m_ * m_, S = s_ * s_ * s_;
  return {to_srgb(+4.0767416621f * L - 3.3077115913f * M + 0.2309699292f * S),
          to_srgb(-1.2684380046f * L + 2.6097574011f * M - 0.3413193965f * S),
          to_srgb(-0.0041960863f * L - 0.7034186147f * M + 1.7076147010f * S), alpha};
}

ImU32 Color::u32() const { return ImGui::GetColorU32(vec4()); }

Color mix(Color a, Color b, float t) {
  t = std::clamp(t, 0.f, 1.f);
  return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t};
}

Color over(Color top, Color bottom) {
  const float a = top.a + bottom.a * (1 - top.a);
  if (a <= 0) return transparent;
  auto ch = [&](float t, float b) { return (t * top.a + b * bottom.a * (1 - top.a)) / a; };
  return {ch(top.r, bottom.r), ch(top.g, bottom.g), ch(top.b, bottom.b), a};
}

} // namespace silta
