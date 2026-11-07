#pragma once
// Stroke icons drawn as vector paths on a 24x24 grid, in the style of Lucide
// (2px strokes, round geometry). No textures: they stay crisp at any scale.
#include "silta/color.hpp"
#include "imgui.h"

namespace silta {

enum class Icon {
  none,
  check,
  x,
  plus,
  minus,
  chevron_right,
  chevron_left,
  chevron_down,
  chevron_up,
  arrow_right,
  mail,
  search,
  loader, // spins
  bell,
  settings,
  user,
};

// Draws `icon` in the square at `min` with side `size_px`.
void draw_icon(ImDrawList *list, Icon icon, ImVec2 min, float size_px, Color color);

} // namespace silta
