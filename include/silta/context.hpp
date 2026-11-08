#pragma once
// Silta runs on top of a Dear ImGui context: ImGui supplies input, IDs, windows,
// draw lists and the renderer backends; Silta supplies the design system.
//
//   ImGui::CreateContext();
//   silta::init();
//   // every frame, inside ImGui::NewFrame() ... ImGui::Render():
//   silta::begin_frame();  ...ui...  silta::end_frame();
//
// Silta's state belongs to the current ImGui context, so several contexts can
// each run Silta. Silta leaves ImGui's global style alone: its components draw
// with Silta's theme, the host's ImGui widgets keep the host's style (unless
// InitOptions::style_imgui asks Silta to theme them as well).
#include <cstddef>
#include "silta/theme.hpp"

namespace silta {

// A font file on disk, or font data in memory (TTF/OTF). With `data`, the
// memory must stay valid while the ImGui context lives.
struct FontSource {
  const char *path = nullptr;
  const void *data = nullptr;
  std::size_t size = 0;
  bool empty() const { return !path && !data; }
};

struct InitOptions {
  Theme theme = Theme::light();
  // UI scale (1 = 96 dpi). Hosts update it on monitor changes with set_scale().
  float scale = 1.f;
  // Fonts. Left empty, Silta uses the Inter faces built into the library
  // (or, in builds without them, Inter-*.ttf from `font_dir`, the executable's
  // "fonts" folder, then the folder Silta was built with).
  FontSource regular, medium, semibold;
  const char *font_dir = nullptr;
  // Also theme ImGui's own widgets and windows, and make Silta's font ImGui's
  // default. Off for hosts with their own ImGui look; silta::App turns it on.
  bool style_imgui = false;
};

struct Fonts {
  ImFont *regular = nullptr;
  ImFont *medium = nullptr;
  ImFont *semibold = nullptr;
  // ImGui sizes a font by ascender - descender; CSS sizes it by the em box.
  // font_size_px * em_to_imgui is the ImGui size that matches CSS font-size.
  float em_to_imgui = 1.f;
};

// Call after ImGui::CreateContext() and before the first frame. Returns false
// when no Inter font could be loaded (ImGui's default font is used instead).
bool init(const InitOptions &options = {});
// Frees the current context's Silta state (also done by ImGui::DestroyContext).
void shutdown();

// Around the UI of each frame: after ImGui::NewFrame(), before ImGui::Render().
void begin_frame();
void end_frame();

const Theme &theme();
void set_theme(const Theme &theme);

// Applies the theme to ImGui's own style (colors, rounding, spacing) and makes
// Silta's regular font the default. Done automatically with style_imgui.
void style_imgui();

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
// Asks for a frame `seconds` from now (timers: tooltip delays, toasts, ...).
void wake_in(double seconds);
// Seconds the host may sleep before the next frame is needed (e.g. caret
// blink). Negative: sleep until there is input.
double idle_timeout();

// Library version, also available as SILTA_VERSION_MAJOR/MINOR/PATCH.
const char *version();

} // namespace silta
