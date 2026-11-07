#pragma once
// Silta runs on top of a Dear ImGui context: ImGui supplies input, IDs, windows,
// draw lists and the renderer backends; Silta supplies the design system.
//
//   ImGui::CreateContext();
//   silta::init({.font_dir = "fonts"});
//   // every frame, inside ImGui::NewFrame() ... ImGui::Render():
//   silta::begin_frame();  ...ui...  silta::end_frame();
#include "silta/theme.hpp"

namespace silta {

struct InitOptions {
  // Folder with Inter-Regular.ttf, Inter-Medium.ttf and Inter-SemiBold.ttf.
  // nullptr: "<executable folder>/fonts", then the folder Silta was built with.
  const char *font_dir = nullptr;
  Theme theme = Theme::light();
  // UI scale (1 = 96 dpi). Hosts update it on monitor changes with set_scale().
  float scale = 1.f;
};

struct Fonts {
  ImFont *regular = nullptr;
  ImFont *medium = nullptr;
  ImFont *semibold = nullptr;
  // ImGui sizes a font by ascender - descender; CSS sizes it by the em box.
  // font_size_px * em_to_imgui is the ImGui size that matches CSS font-size.
  float em_to_imgui = 1.f;
};

// Call after ImGui::CreateContext() and before the first frame.
bool init(const InitOptions &options = {});
void shutdown();

// Around the UI of each frame: after ImGui::NewFrame(), before ImGui::Render().
void begin_frame();
void end_frame();

const Theme &theme();
// Takes effect immediately; ImGui's own colors follow.
void set_theme(const Theme &theme);

float scale();
void set_scale(float scale);
// Points to pixels: px(16) is 16 points at the current scale.
inline float px(float points) { return points * scale(); }

const Fonts &fonts();

// Rendering on demand. Silta knows when something still moves (animations,
// layout settling, blinking caret); hosts render while this is true and
// otherwise wait for input.
bool wants_frame();
void request_frames(int count = 1);
// Seconds the host may sleep before the next frame is needed (e.g. caret
// blink). Negative: sleep until there is input.
double idle_timeout();

} // namespace silta
