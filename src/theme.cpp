#include "silta/theme.hpp"

namespace silta {
namespace {
struct Lch {
  float l, c, h;
  Color color(float alpha = 1) const { return Color::oklch(l, c, h, alpha); }
};

// The tokens that differ between shadcn's base colors (Tailwind v4 values).
// Everything else derives from these the way shadcn's CSS does.
struct LightScale {
  Lch foreground, primary, primary_foreground, secondary, muted_foreground, border, ring;
};
struct DarkScale {
  Lch background, foreground, card, primary, primary_foreground, secondary, muted_foreground, ring;
};

LightScale light_scale(BaseColor base) {
  switch (base) {
  case BaseColor::zinc:
    return {{0.141f, 0.005f, 285.823f}, {0.21f, 0.006f, 285.885f}, {0.985f, 0, 0},
            {0.967f, 0.001f, 286.375f}, {0.552f, 0.016f, 285.938f}, {0.92f, 0.004f, 286.32f},
            {0.705f, 0.015f, 286.067f}};
  case BaseColor::slate:
    return {{0.129f, 0.042f, 264.695f}, {0.208f, 0.042f, 265.755f}, {0.984f, 0.003f, 247.858f},
            {0.968f, 0.007f, 247.896f}, {0.554f, 0.046f, 257.417f}, {0.929f, 0.013f, 255.508f},
            {0.704f, 0.04f, 256.788f}};
  case BaseColor::stone:
    return {{0.147f, 0.004f, 49.25f},   {0.216f, 0.006f, 56.043f},  {0.985f, 0.001f, 106.423f},
            {0.97f, 0.001f, 106.424f},  {0.553f, 0.013f, 58.071f},  {0.923f, 0.003f, 48.717f},
            {0.709f, 0.01f, 56.259f}};
  case BaseColor::gray:
    return {{0.13f, 0.028f, 261.692f},  {0.21f, 0.034f, 264.665f},  {0.985f, 0.002f, 247.839f},
            {0.967f, 0.003f, 264.542f}, {0.551f, 0.027f, 264.364f}, {0.928f, 0.006f, 264.531f},
            {0.707f, 0.022f, 261.325f}};
  case BaseColor::neutral:
    break;
  }
  return {{0.145f, 0, 0}, {0.205f, 0, 0}, {0.985f, 0, 0}, {0.97f, 0, 0},
          {0.556f, 0, 0}, {0.922f, 0, 0}, {0.708f, 0, 0}};
}

DarkScale dark_scale(BaseColor base) {
  switch (base) {
  case BaseColor::zinc:
    return {{0.141f, 0.005f, 285.823f}, {0.985f, 0, 0},            {0.21f, 0.006f, 285.885f},
            {0.92f, 0.004f, 286.32f},   {0.21f, 0.006f, 285.885f}, {0.274f, 0.006f, 286.033f},
            {0.705f, 0.015f, 286.067f}, {0.552f, 0.016f, 285.938f}};
  case BaseColor::slate:
    return {{0.129f, 0.042f, 264.695f}, {0.984f, 0.003f, 247.858f}, {0.208f, 0.042f, 265.755f},
            {0.929f, 0.013f, 255.508f}, {0.208f, 0.042f, 265.755f}, {0.279f, 0.041f, 260.031f},
            {0.704f, 0.04f, 256.788f},  {0.551f, 0.027f, 264.364f}};
  case BaseColor::stone:
    return {{0.147f, 0.004f, 49.25f},  {0.985f, 0.001f, 106.423f}, {0.216f, 0.006f, 56.043f},
            {0.923f, 0.003f, 48.717f}, {0.216f, 0.006f, 56.043f},  {0.268f, 0.007f, 34.298f},
            {0.709f, 0.01f, 56.259f},  {0.553f, 0.013f, 58.071f}};
  case BaseColor::gray:
    return {{0.13f, 0.028f, 261.692f},  {0.985f, 0.002f, 247.839f}, {0.21f, 0.034f, 264.665f},
            {0.928f, 0.006f, 264.531f}, {0.21f, 0.034f, 264.665f},  {0.278f, 0.033f, 256.848f},
            {0.707f, 0.022f, 261.325f}, {0.551f, 0.027f, 264.364f}};
  case BaseColor::neutral:
    break;
  }
  return {{0.145f, 0, 0}, {0.985f, 0, 0}, {0.205f, 0, 0}, {0.922f, 0, 0},
          {0.205f, 0, 0}, {0.269f, 0, 0}, {0.708f, 0, 0}, {0.556f, 0, 0}};
}
} // namespace

Theme Theme::make(BaseColor base, Mode mode) {
  Theme t;
  t.mode = mode;
  if (mode == Mode::light) {
    const LightScale s = light_scale(base);
    t.background = white;
    t.foreground = s.foreground.color();
    t.card = t.popover = white;
    t.card_foreground = t.popover_foreground = t.foreground;
    t.primary = s.primary.color();
    t.primary_foreground = s.primary_foreground.color();
    t.secondary = t.muted = t.accent = s.secondary.color();
    t.secondary_foreground = t.accent_foreground = t.primary;
    t.muted_foreground = s.muted_foreground.color();
    t.destructive = Color::oklch(0.577f, 0.245f, 27.325f);
    t.border = t.input = s.border.color();
    t.ring = s.ring.color();
  } else {
    const DarkScale s = dark_scale(base);
    t.background = s.background.color();
    t.foreground = s.foreground.color();
    t.card = t.popover = s.card.color();
    t.card_foreground = t.popover_foreground = t.foreground;
    t.primary = s.primary.color();
    t.primary_foreground = s.primary_foreground.color();
    t.secondary = t.muted = t.accent = s.secondary.color();
    t.secondary_foreground = t.accent_foreground = t.foreground;
    t.muted_foreground = s.muted_foreground.color();
    t.destructive = Color::oklch(0.704f, 0.191f, 22.216f);
    t.border = white.with_alpha(0.10f);
    t.input = white.with_alpha(0.15f);
    t.ring = s.ring.color();
  }
  t.destructive_foreground = white;
  return t;
}

Color contrast_on(Color c) {
  // WCAG relative luminance on the sRGB values (good enough for picking ink).
  const float luma = 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b;
  return luma > 0.55f ? Color::oklch(0.145f, 0, 0) : Color::oklch(0.985f, 0, 0);
}

Theme Theme::with_primary(Color primary_color) const {
  return with_primary(primary_color, contrast_on(primary_color));
}

Theme Theme::with_primary(Color primary_color, Color foreground_on_primary) const {
  Theme t = *this;
  t.primary = primary_color;
  t.primary_foreground = foreground_on_primary;
  t.ring = primary_color.fade(0.6f);
  return t;
}

} // namespace silta
