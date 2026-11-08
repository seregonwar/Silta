#include "silta/text.hpp"
#include "silta/context.hpp"
#include "silta/layout.hpp"
#include "shaper.hpp"
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstring>

namespace silta {
namespace {
struct Scale {
  float font, line;
};
constexpr Scale scale_table[] = {{12, 16}, {14, 20}, {16, 24}, {18, 28}, {20, 28}, {24, 32}, {30, 36}, {36, 40}};

int weight_index(Weight w) { return w == Weight::semibold ? 2 : w == Weight::medium ? 1 : 0; }

// How a piece of text is measured and drawn: by ImGui's atlas, or shaped
// (complex scripts, right-to-left text, emoji sequences).
struct Run {
  ImFont *font;
  float size;
  int weight;
  bool shaped;
  Run(const char *text, Weight w, float fs)
      : font(font_for(w)), size(fs), weight(weight_index(w)),
        shaped(detail::shaping_available() && detail::needs_shaping(text, text + std::strlen(text))) {}
  const char *wrap(const char *b, const char *e, float wrap_width) const {
    return shaped ? detail::shaped_wrap(b, e, weight, size, wrap_width) : font->CalcWordWrapPosition(size, b, e, wrap_width);
  }
  float width(const char *b, const char *e) const {
    if (e <= b) return 0;
    return shaped ? detail::shaped_width(b, e, weight, size) : font->CalcTextSizeA(size, FLT_MAX, 0, b, e).x;
  }
  void draw(ImDrawList *list, ImVec2 pos, const char *b, const char *e, ImU32 col) const {
    if (e <= b) return;
    if (shaped) detail::draw_shaped(list, pos, b, e, weight, size, col);
    else list->AddText(font, size, pos, col, b, e);
  }
};

// Calls fn(begin, end) for each line of `text` wrapped at `wrap_width`.
template <class Fn> void for_each_line(const Run &run, const char *text, float wrap_width, Fn fn) {
  const char *end = text + std::strlen(text);
  if (text == end) {
    fn(text, text);
    return;
  }
  const char *s = text;
  while (s < end) {
    const char *nl = static_cast<const char *>(std::memchr(s, '\n', size_t(end - s)));
    const char *para_end = nl ? nl : end;
    const char *line_end = wrap_width > 0 && para_end > s ? run.wrap(s, para_end, wrap_width) : para_end;
    if (line_end > para_end) line_end = para_end;
    if (line_end == s && *s != '\n') line_end = s + 1; // a word wider than the box
    fn(s, line_end);
    s = line_end;
    if (s < end && *s == '\n') ++s;
    else
      while (s < end && *s == ' ') ++s;
  }
}

float line_px(TextSize size, float line_height) {
  return std::round(px(line_height > 0 ? line_height : line_points(size)));
}
} // namespace

float font_points(TextSize size) { return scale_table[int(size)].font; }
float line_points(TextSize size) { return scale_table[int(size)].line; }

ImFont *font_for(Weight weight) {
  const Fonts &f = fonts();
  ImFont *font = weight == Weight::semibold ? f.semibold : weight == Weight::medium ? f.medium : f.regular;
  return font ? font : ImGui::GetFont();
}

float imgui_font_size(float points) { return px(points) * fonts().em_to_imgui; }

ImVec2 measure_text(const char *text, TextSize size, Weight weight, float wrap_width, float line_height) {
  const Run run(text, weight, imgui_font_size(font_points(size)));
  const float lh = line_px(size, line_height);
  float width = 0;
  int lines = 0;
  for_each_line(run, text, wrap_width, [&](const char *b, const char *e) {
    width = std::max(width, run.width(b, e));
    ++lines;
  });
  return {std::ceil(width), lh * float(lines)};
}

ImVec2 draw_text(ImDrawList *list, ImVec2 top_left, const char *text, TextSize size, Weight weight, Color color,
                 float wrap_width, float line_height) {
  const Run run(text, weight, imgui_font_size(font_points(size)));
  const float fs = run.size;
  const float lh = line_px(size, line_height);
  // The glyph box (ascender..descender) is centred in the line box, as CSS does.
  const float lead = std::round((lh - fs) / 2);
  const ImU32 col = color.u32();
  float y = top_left.y, width = 0;
  for_each_line(run, text, wrap_width, [&](const char *b, const char *e) {
    run.draw(list, ImVec2(std::round(top_left.x), y + lead), b, e, col);
    width = std::max(width, run.width(b, e));
    y += lh;
  });
  return {std::ceil(width), y - top_left.y};
}

void Text(const char *text, const TextOptions &o) {
  const Theme &t = theme();
  const Color color = o.color ? *o.color : o.muted ? t.muted_foreground : t.foreground;
  const float wrap = o.wrap ? layout::fill_width() : 0;
  const ImVec2 size = measure_text(text, o.size, o.weight, wrap, o.line_height);
  const ImRect r = layout::allocate(ImVec2(wrap > 0 ? std::max(size.x, wrap) : size.x, size.y));
  draw_text(ImGui::GetWindowDrawList(), r.Min, text, o.size, o.weight, color, wrap, o.line_height);
}

void Label(const char *text) {
  Text(text, {.size = TextSize::sm, .weight = Weight::medium, .line_height = font_points(TextSize::sm)});
}

} // namespace silta
