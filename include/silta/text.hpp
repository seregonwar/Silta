#pragma once
// Typography with Tailwind's scale: sizes are CSS font sizes in points, and
// every size has its line height, so text lines up like it does on the web.
#include <optional>
#include "silta/color.hpp"
#include "imgui.h"

namespace silta {

enum class TextSize { xs, sm, base, lg, xl, xl2, xl3, xl4 };
enum class Weight { regular, medium, semibold };

// CSS font-size and line-height in points (text-sm = 14 / 20).
float font_points(TextSize size);
float line_points(TextSize size);

ImFont *font_for(Weight weight);
// ImGui font size, in pixels at the current scale, that renders like CSS
// font-size `points`.
float imgui_font_size(float points);

// Size of the text box. `wrap_width` > 0 wraps (pixels). `line_height` in
// points overrides the size's line height (CSS `leading-*`; `leading-none` is
// the font size).
ImVec2 measure_text(const char *text, TextSize size, Weight weight = Weight::regular, float wrap_width = 0,
                    float line_height = 0);

// Draws text with each line's glyphs vertically centred in its line box, as
// CSS does. Returns the box size.
ImVec2 draw_text(ImDrawList *list, ImVec2 top_left, const char *text, TextSize size, Weight weight, Color color,
                 float wrap_width = 0, float line_height = 0);

struct TextOptions {
  TextSize size = TextSize::sm;
  Weight weight = Weight::regular;
  std::optional<Color> color; // default: theme().foreground
  bool wrap = false;          // wrap to the available width
  bool muted = false;         // shorthand for color = muted_foreground
  float line_height = 0;      // points; 0 = the size's own line height
};

// A paragraph or label placed by the current layout.
void Text(const char *text, const TextOptions &options = {});

// shadcn Label: text-sm, font-medium, leading-none.
void Label(const char *text);

} // namespace silta
