#pragma once
// Design tokens. Same names and meaning as shadcn/ui's CSS variables, so a
// shadcn theme maps over one to one:
//   --background -> background, --primary-foreground -> primary_foreground, ...
#include "silta/color.hpp"

namespace silta {

// shadcn/ui base colors.
enum class BaseColor { neutral, zinc, slate, stone, gray };
enum class Mode { light, dark };

struct Theme {
  Mode mode = Mode::light;

  Color background, foreground;
  Color card, card_foreground;
  Color popover, popover_foreground;
  Color primary, primary_foreground;
  Color secondary, secondary_foreground;
  Color muted, muted_foreground;
  Color accent, accent_foreground;
  Color destructive, destructive_foreground;
  Color border, input, ring;

  // --radius, in points (0.625rem). The scale below mirrors Tailwind v4.
  float radius = 10.f;

  bool is_dark() const { return mode == Mode::dark; }
  float radius_sm() const { return radius - 4; }
  float radius_md() const { return radius - 2; }
  float radius_lg() const { return radius; }
  float radius_xl() const { return radius + 4; }

  static Theme make(BaseColor base, Mode mode);
  static Theme light(BaseColor base = BaseColor::neutral) { return make(base, Mode::light); }
  static Theme dark(BaseColor base = BaseColor::neutral) { return make(base, Mode::dark); }

  // Brand accent on top of a base theme: primary, its foreground (picked for
  // contrast when not given) and the focus ring follow it.
  Theme with_primary(Color primary_color) const;
  Theme with_primary(Color primary_color, Color foreground_on_primary) const;
};

// Readable text color (near black or near white) on top of `background`.
Color contrast_on(Color background);

} // namespace silta
