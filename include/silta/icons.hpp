#pragma once
// Stroke icons drawn as vector paths on a 24x24 grid, in the style of Lucide
// (2px strokes, round geometry). No textures: they stay crisp at any scale.
#include "silta/color.hpp"
#include "imgui.h"

namespace silta {

enum class Icon {
  none,
  // Interface
  check, x, plus, minus, chevron_right, chevron_left, chevron_down, chevron_up, chevrons_up_down,
  arrow_right, arrow_left, more_horizontal, more_vertical, grip_vertical, search, settings, user, bell,
  info, alert, sun, moon, maximize, copy, eye, eye_off, lock, unlock, mail, loader, dot,
  // Files and media
  folder_open, upload, download, film, image, music, type, layers,
  // Playback and editing
  play, pause, skip_back, skip_forward, volume, volume_off, scissors, trash, undo, redo, zoom_in, zoom_out,
  magnet, pointer,
};

// Draws `icon` in the square at `min` with side `size_px`. `filled` fills the
// closed shapes (play, pause, skip) the way media transports usually do.
void draw_icon(ImDrawList *list, Icon icon, ImVec2 min, float size_px, Color color, bool filled = false);

} // namespace silta
