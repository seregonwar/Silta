#pragma once
// Text shaping for complex scripts and bidirectional text (internal).
//
// Latin, Greek, Cyrillic, CJK and most symbols are drawn by ImGui's font
// atlas, which maps one codepoint to one glyph. Scripts whose glyphs depend on
// their neighbours (Arabic joining, Indic conjuncts and vowel signs, Thai
// marks), right-to-left text and emoji sequences need shaping: those runs go
// through this module instead. It orders the text with the Unicode bidi
// algorithm (SheenBidi), splits it by font (Inter, then the fallback chain),
// shapes each piece with HarfBuzz and rasterizes the glyphs with FreeType into
// an atlas drawn through silta::Texture, so every renderer backend works.
#include "imgui.h"
#include <cstddef>
#include <vector>

namespace silta::detail {

// True when this build can shape text.
bool shaping_available();
// True when [begin, end) contains text the ImGui atlas cannot draw correctly.
bool needs_shaping(const char *begin, const char *end);

// One line (no line breaks inside), at ImGui font size `size` (pixels).
// weight: 0 regular, 1 medium, 2 semibold.
float shaped_width(const char *begin, const char *end, int weight, float size, bool rtl_base);
// Where to break [begin, end) so the first line fits `wrap_width` (at spaces
// when possible). Returns a pointer in (begin, end].
const char *shaped_wrap(const char *begin, const char *end, int weight, float size, float wrap_width,
                        bool rtl_base);
// Draws the line with its glyph box (ascender..descender) starting at `top_left`.
void draw_shaped(ImDrawList *list, ImVec2 top_left, const char *begin, const char *end, int weight, float size,
                 ImU32 color, bool rtl_base);

// Visual clusters and caret stops of one shaped line, in visual left-to-right
// order. `begin`/`end` and caret offsets are byte offsets from the line start.
struct ShapedCluster {
  std::size_t begin = 0, end = 0;
  float x0 = 0, x1 = 0;
  int level = 0;
};
struct ShapedCaret {
  std::size_t offset = 0;
  float x = 0;
  int level = 0;
};
struct ShapedLineInfo {
  float width = 0;
  std::vector<ShapedCluster> clusters;
  std::vector<ShapedCaret> carets;
};
// Describes one line for a text editor. False when this build has no shaping.
bool shaped_line_info(const char *begin, const char *end, int weight, float size, bool rtl_base,
                      ShapedLineInfo &out);

} // namespace silta::detail
