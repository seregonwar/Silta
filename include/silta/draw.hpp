#pragma once
// Drawing primitives with CSS semantics, built only on ImDrawList so they look
// the same on every renderer backend.
#include "silta/color.hpp"
#include "imgui.h"
#include "imgui_internal.h"

namespace silta::draw {

// CSS box-shadow: offset, blur and spread in points.
struct Shadow {
  float x = 0, y = 0, blur = 0, spread = 0;
  Color color = transparent;
};

// Tailwind v4 shadow scale (each is one or two box-shadows).
struct ShadowStack {
  Shadow layers[2];
  int count = 0;
};
ShadowStack shadow_xs();
ShadowStack shadow_sm();
ShadowStack shadow_md();
ShadowStack shadow_lg();

void shadow(ImDrawList *list, const ImRect &box, float radius_px, const Shadow &s);
void shadow(ImDrawList *list, const ImRect &box, float radius_px, const ShadowStack &s);

// Filled rounded box.
void fill(ImDrawList *list, const ImRect &box, Color color, float radius_px, ImDrawFlags flags = 0);
// CSS border: `width_px` drawn inside the box, like box-sizing: border-box.
void border(ImDrawList *list, const ImRect &box, Color color, float radius_px, float width_px = 1);
// shadcn focus ring: 3px of ring/50 outside the box.
void focus_ring(ImDrawList *list, const ImRect &box, float radius_px, Color ring);

} // namespace silta::draw
