#include "shaper.hpp"
#include "fonts.hpp"
#include "per_context.hpp"
#include "silta/context.hpp"
#include "silta/texture.hpp"
#include "imgui_internal.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

#ifdef SILTA_SHAPING
#include <SheenBidi/SheenBidi.h>
#include <hb.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#endif

namespace silta::detail {
namespace {
// Codepoints whose rendering depends on shaping or bidi reordering.
bool complex_codepoint(uint32_t c) {
  return (c >= 0x0300 && c <= 0x036F) ||   // combining diacritics
         (c >= 0x0590 && c <= 0x08FF) ||   // Hebrew, Arabic, Syriac, Thaana, NKo, ...
         (c >= 0x0900 && c <= 0x0DFF) ||   // Indic scripts
         (c >= 0x0E00 && c <= 0x0FFF) ||   // Thai, Lao, Tibetan
         (c >= 0x1000 && c <= 0x109F) ||   // Myanmar
         (c >= 0x1780 && c <= 0x17FF) ||   // Khmer
         (c >= 0x1AB0 && c <= 0x1AFF) || (c >= 0x1DC0 && c <= 0x1DFF) || // more combining marks
         (c >= 0x200C && c <= 0x200F) ||   // ZWNJ, ZWJ, LRM, RLM
         (c >= 0x202A && c <= 0x202E) || (c >= 0x2066 && c <= 0x2069) || // bidi controls
         (c >= 0xA8E0 && c <= 0xA8FF) ||   // Devanagari extended
         (c >= 0xFB1D && c <= 0xFDFF) || (c >= 0xFE70 && c <= 0xFEFF) || // presentation forms
         c == 0xFE0F ||                    // emoji presentation selector
         (c >= 0x1F1E6 && c <= 0x1F1FF) || // regional indicators (flags)
         (c >= 0x1F3FB && c <= 0x1F3FF) || // skin tones
         (c >= 0x10800 && c <= 0x10FFF);   // historic RTL scripts
}
} // namespace

bool needs_shaping(const char *begin, const char *end) {
  for (const char *s = begin; s < end;) {
    if (static_cast<unsigned char>(*s) < 0x80) {
      ++s;
      continue;
    }
    unsigned int c = 0;
    const int n = ImTextCharFromUtf8(&c, s, end);
    if (n <= 0) break;
    s += n;
    if (complex_codepoint(c)) return true;
  }
  return false;
}

#ifndef SILTA_SHAPING

bool shaping_available() { return false; }
float shaped_width(const char *, const char *, int, float) { return 0; }
const char *shaped_wrap(const char *, const char *end, int, float, float) { return end; }
void draw_shaped(ImDrawList *, ImVec2, const char *, const char *, int, float, ImU32) {}

#else

namespace {
struct Face {
  FT_Face ft = nullptr;
  hb_face_t *hb_face = nullptr;
  hb_font_t *hb = nullptr;
  float upem = 1000, asc = 800, desc = -200; // font units
  bool color = false, scalable = true;
  // ImGui sizes a face so ascender - descender equals the font size; so do we.
  float ppem(float size) const { return size * upem / std::max(1.f, asc - desc); }
};

struct Slot {
  int x = 0, y = 0, w = 0, h = 0;  // in the atlas
  float left = 0, top = 0;         // bitmap bearing, pixels at the drawn size
  float draw_w = 0, draw_h = 0;    // drawn size (bitmap strikes are scaled)
  bool color = false;
};

struct Glyph {
  int face;
  uint32_t id;
  float x, y; // pen offset (pixels) and baseline offset (up is negative)
};

struct Line {
  std::vector<Glyph> glyphs;
  float width = 0;
};

struct ShaperState {
  FT_Library library = nullptr;
  std::vector<Face> faces; // 0..2: Inter weights; then the fallback chain
  bool ready = false;
  hb_buffer_t *buffer = nullptr;
  std::vector<uint32_t> codepoints;
  // Glyph atlas (RGBA, white glyphs with alpha; colour glyphs as they are).
  int atlas_w = 1024, atlas_h = 1024;
  std::vector<uint8_t> pixels;
  int pen_x = 1, pen_y = 1, row_h = 0;
  bool dirty = false;
  Texture texture;
  std::unordered_map<uint64_t, Slot> slots;
  std::unordered_map<std::string, Line> lines;

  ~ShaperState() {
    for (Face &f : faces) {
      if (f.hb) hb_font_destroy(f.hb);
      if (f.hb_face) hb_face_destroy(f.hb_face);
      if (f.ft) FT_Done_Face(f.ft);
    }
    if (buffer) hb_buffer_destroy(buffer);
    if (library) FT_Done_FreeType(library);
  }

  Face make_face(const FaceData &d) {
    Face f;
    if (!d.data) return f;
    if (FT_New_Memory_Face(library, d.data, FT_Long(d.size), d.index, &f.ft) != 0) {
      f.ft = nullptr;
      return f;
    }
    hb_blob_t *blob = hb_blob_create(reinterpret_cast<const char *>(d.data), unsigned(d.size),
                                     HB_MEMORY_MODE_READONLY, nullptr, nullptr);
    f.hb_face = hb_face_create(blob, unsigned(d.index));
    hb_blob_destroy(blob);
    f.hb = hb_font_create(f.hb_face);
    f.upem = float(hb_face_get_upem(f.hb_face));
    f.scalable = FT_IS_SCALABLE(f.ft);
    f.color = d.color || FT_HAS_COLOR(f.ft);
    if (f.ft->ascender != 0 || f.ft->descender != 0) f.asc = float(f.ft->ascender), f.desc = float(f.ft->descender);
    else f.asc = f.upem * 0.8f, f.desc = -f.upem * 0.2f;
    return f;
  }

  void ensure_ready() {
    if (ready) return;
    ready = true;
    if (FT_Init_FreeType(&library) != 0) return;
    buffer = hb_buffer_create();
    for (int w = 0; w < 3; ++w) faces.push_back(make_face(inter_face(w)));
    for (const FaceData &d : fallback_faces()) faces.push_back(make_face(d));
    pixels.assign(size_t(atlas_w) * atlas_h * 4, 0);
  }

  bool has_glyph(int face, uint32_t c) {
    hb_codepoint_t g = 0;
    return faces[size_t(face)].hb && hb_font_get_nominal_glyph(faces[size_t(face)].hb, c, &g);
  }

  // The first face of the chain (this weight, then the fallbacks) with `c`.
  int face_for(uint32_t c, int weight) {
    if (has_glyph(weight, c)) return weight;
    for (int i = 3; i < int(faces.size()); ++i)
      if (has_glyph(i, c)) return i;
    return weight;
  }

  // ------------------------------------------------------------- shaping
  // Shapes one line: bidi runs in visual order, each split by font and shaped.
  const Line &shape(const char *begin, const char *end, int weight, float size) {
    std::string key(1, char('0' + weight));
    key.append(reinterpret_cast<const char *>(&size), sizeof size);
    key.append(begin, end);
    if (auto it = lines.find(key); it != lines.end()) return it->second;
    if (lines.size() > 4000) lines.clear(); // simple bound; lines are cheap to reshape
    Line &line = lines[key];
    if (!buffer || faces.empty()) return line;

    codepoints.clear();
    for (const char *s = begin; s < end;) {
      unsigned int c = 0;
      const int n = ImTextCharFromUtf8(&c, s, end);
      if (n <= 0) break;
      s += n;
      codepoints.push_back(c);
    }
    if (codepoints.empty()) return line;

    SBCodepointSequence sequence = {SBStringEncodingUTF32, codepoints.data(), codepoints.size()};
    SBAlgorithmRef algorithm = SBAlgorithmCreate(&sequence);
    SBParagraphRef paragraph = SBAlgorithmCreateParagraph(algorithm, 0, codepoints.size(), SBLevelDefaultLTR);
    SBLineRef bidi_line = SBParagraphCreateLine(paragraph, 0, SBParagraphGetLength(paragraph));
    const SBUInteger run_count = SBLineGetRunCount(bidi_line);
    const SBRun *runs = SBLineGetRunsPtr(bidi_line);

    float pen = 0;
    struct Segment {
      size_t start, length;
      int face;
    };
    std::vector<Segment> segments;
    for (SBUInteger r = 0; r < run_count; ++r) { // visual order
      const bool rtl = (runs[r].level & 1) != 0;
      const size_t start = runs[r].offset, length = runs[r].length;
      // Split the run by font, in logical order. Marks, joiners and
      // selectors stay with the character before them.
      segments.clear();
      for (size_t i = start; i < start + length; ++i) {
        const uint32_t c = codepoints[i];
        const SBGeneralCategory gc = SBCodepointGetGeneralCategory(c);
        const bool attaches = SBGeneralCategoryIsMark(gc) || c == 0x200C || c == 0x200D || (c >= 0xFE00 && c <= 0xFE0F) ||
                              (c >= 0x1F3FB && c <= 0x1F3FF) || (c >= 0xE0100 && c <= 0xE01EF);
        const bool neutral = c == ' ' || c == 0xA0;
        int face = (attaches || neutral) && !segments.empty() ? segments.back().face : face_for(c, weight);
        if (!segments.empty() && segments.back().face == face) ++segments.back().length;
        else segments.push_back({i, 1, face});
      }
      if (rtl) std::reverse(segments.begin(), segments.end()); // right to left
      for (const Segment &seg : segments) {
        Face &f = faces[size_t(seg.face)];
        if (!f.hb) continue;
        hb_buffer_clear_contents(buffer);
        hb_buffer_add_utf32(buffer, codepoints.data(), int(codepoints.size()), unsigned(seg.start), int(seg.length));
        hb_buffer_set_direction(buffer, rtl ? HB_DIRECTION_RTL : HB_DIRECTION_LTR);
        hb_buffer_guess_segment_properties(buffer);
        const int scale = int(std::lround(f.ppem(size) * 64));
        hb_font_set_scale(f.hb, scale, scale);
        hb_shape(f.hb, buffer, nullptr, 0);
        unsigned count = 0;
        const hb_glyph_info_t *info = hb_buffer_get_glyph_infos(buffer, &count);
        const hb_glyph_position_t *pos = hb_buffer_get_glyph_positions(buffer, &count);
        for (unsigned g = 0; g < count; ++g) {
          line.glyphs.push_back({seg.face, info[g].codepoint, pen + pos[g].x_offset / 64.f, -pos[g].y_offset / 64.f});
          pen += pos[g].x_advance / 64.f;
        }
      }
    }
    line.width = pen;
    SBLineRelease(bidi_line);
    SBParagraphRelease(paragraph);
    SBAlgorithmRelease(algorithm);
    return line;
  }

  // ------------------------------------------------------- rasterization
  bool reserve(int w, int h, int &x, int &y) {
    if (w + 2 > atlas_w) return false;
    if (pen_x + w + 1 > atlas_w) pen_x = 1, pen_y += row_h + 1, row_h = 0;
    while (pen_y + h + 1 > atlas_h) {
      if (atlas_h >= 4096) { // full: start over (glyphs are re-rasterized on demand)
        std::fill(pixels.begin(), pixels.end(), uint8_t(0));
        slots.clear();
        pen_x = pen_y = 1, row_h = 0;
        break;
      }
      atlas_h *= 2; // grow; slots keep their pixel coordinates
      pixels.resize(size_t(atlas_w) * atlas_h * 4, 0);
    }
    x = pen_x, y = pen_y;
    pen_x += w + 1;
    row_h = std::max(row_h, h);
    return true;
  }

  const Slot &slot(int face_index, uint32_t id, float size) {
    const uint32_t size_q = uint32_t(std::lround(size * 4));
    const uint64_t key = (uint64_t(face_index) << 48) | (uint64_t(size_q & 0xFFFFFF) << 24) | (id & 0xFFFFFF);
    if (auto it = slots.find(key); it != slots.end()) return it->second;
    Slot &s = slots[key];
    Face &f = faces[size_t(face_index)];
    if (!f.ft) return s;

    float scale = 1;
    if (f.scalable) {
      FT_Size_RequestRec req = {FT_SIZE_REQUEST_TYPE_REAL_DIM, 0, FT_Long(std::lround(size * 64)), 0, 0};
      if (FT_Request_Size(f.ft, &req) != 0) return s;
    } else if (f.ft->num_fixed_sizes > 0) { // bitmap strikes (some emoji fonts)
      int best = 0;
      for (int i = 1; i < f.ft->num_fixed_sizes; ++i)
        if (f.ft->available_sizes[i].y_ppem > f.ft->available_sizes[best].y_ppem) best = i;
      FT_Select_Size(f.ft, best);
      scale = f.ppem(size) / (float(f.ft->available_sizes[best].y_ppem) / 64.f);
    }
    FT_Int32 flags = f.scalable ? FT_LOAD_TARGET_LIGHT : FT_LOAD_DEFAULT;
    if (f.color) flags |= FT_LOAD_COLOR;
    if (FT_Load_Glyph(f.ft, id, flags) != 0) return s;
    FT_GlyphSlot g = f.ft->glyph;
    if (g->format != FT_GLYPH_FORMAT_BITMAP && FT_Render_Glyph(g, FT_RENDER_MODE_NORMAL) != 0) return s;
    const FT_Bitmap &bm = g->bitmap;
    s.left = float(g->bitmap_left) * scale;
    s.top = float(g->bitmap_top) * scale;
    s.draw_w = float(bm.width) * scale;
    s.draw_h = float(bm.rows) * scale;
    s.color = bm.pixel_mode == FT_PIXEL_MODE_BGRA;
    if (bm.width == 0 || bm.rows == 0) return s;
    int x = 0, y = 0;
    if (!reserve(int(bm.width), int(bm.rows), x, y)) return s;
    s.x = x, s.y = y, s.w = int(bm.width), s.h = int(bm.rows);
    for (unsigned row = 0; row < bm.rows; ++row) {
      const uint8_t *src = bm.buffer + int(row) * bm.pitch;
      uint8_t *dst = &pixels[(size_t(y + int(row)) * atlas_w + x) * 4];
      for (unsigned col = 0; col < bm.width; ++col, dst += 4) {
        if (bm.pixel_mode == FT_PIXEL_MODE_BGRA) { // premultiplied BGRA
          const uint8_t *p = src + col * 4;
          const unsigned a = p[3];
          dst[0] = uint8_t(a ? std::min(255u, p[2] * 255u / a) : 0);
          dst[1] = uint8_t(a ? std::min(255u, p[1] * 255u / a) : 0);
          dst[2] = uint8_t(a ? std::min(255u, p[0] * 255u / a) : 0);
          dst[3] = uint8_t(a);
        } else if (bm.pixel_mode == FT_PIXEL_MODE_MONO) {
          const bool on = (src[col >> 3] >> (7 - (col & 7))) & 1;
          dst[0] = dst[1] = dst[2] = 255, dst[3] = on ? 255 : 0;
        } else { // FT_PIXEL_MODE_GRAY
          dst[0] = dst[1] = dst[2] = 255, dst[3] = src[col];
        }
      }
    }
    dirty = true;
    return s;
  }
};

ShaperState &S() {
  ShaperState &s = per_context<ShaperState>();
  s.ensure_ready();
  return s;
}
} // namespace

bool shaping_available() { return true; }

float shaped_width(const char *begin, const char *end, int weight, float size) {
  return S().shape(begin, end, weight, size).width;
}

const char *shaped_wrap(const char *begin, const char *end, int weight, float size, float wrap_width) {
  ShaperState &st = S();
  auto fits = [&](const char *e) {
    const char *t = e;
    while (t > begin && t[-1] == ' ') --t; // trailing spaces do not count
    return st.shape(begin, t, weight, size).width <= wrap_width;
  };
  if (fits(end)) return end;
  // The last space-separated break that fits.
  const char *best = nullptr;
  for (const char *s = begin; s < end; ++s)
    if (*s == ' ' && s > begin) {
      if (!fits(s)) break;
      best = s + 1;
    }
  if (best) return best;
  // No space fits (one long word, or scripts without spaces like Thai):
  // break between characters, never before a combining mark.
  const char *last = nullptr;
  for (const char *s = begin; s < end;) {
    unsigned int c = 0;
    const int n = ImTextCharFromUtf8(&c, s, end);
    if (n <= 0) break;
    const char *next = s + n;
    unsigned int following = 0;
    if (next < end) ImTextCharFromUtf8(&following, next, end);
    const bool mark_follows = next < end && SBGeneralCategoryIsMark(SBCodepointGetGeneralCategory(following));
    if (!mark_follows) {
      if (!fits(next)) break;
      last = next;
    }
    s = next;
  }
  if (last) return last;
  // Not even one character fits: take one anyway, so the text advances.
  unsigned int c = 0;
  const int n = ImTextCharFromUtf8(&c, begin, end);
  return begin + std::max(1, n);
}

void draw_shaped(ImDrawList *list, ImVec2 top_left, const char *begin, const char *end, int weight, float size,
                 ImU32 color) {
  ShaperState &st = S();
  const Line &line = st.shape(begin, end, weight, size);
  if (line.glyphs.empty()) return;
  // Rasterize first, so the atlas is complete before it is uploaded.
  struct Quad {
    const Slot *slot;
    ImVec2 pos;
  };
  std::vector<Quad> quads;
  quads.reserve(line.glyphs.size());
  const Face &base = st.faces[size_t(std::clamp(weight, 0, 2))];
  const float baseline = top_left.y + size * base.asc / std::max(1.f, base.asc - base.desc);
  for (const Glyph &g : line.glyphs) {
    const Slot &s = st.slot(g.face, g.id, size);
    if (s.w == 0 || s.h == 0) continue;
    quads.push_back({&s, ImVec2(std::round(top_left.x + g.x + s.left), std::round(baseline + g.y - s.top))});
  }
  if (quads.empty()) return;
  if (st.dirty) {
    st.texture.update(st.pixels.data(), st.atlas_w, st.atlas_h);
    st.dirty = false;
  }
  const float iw = 1.f / float(st.atlas_w), ih = 1.f / float(st.atlas_h);
  const ImU32 colour_tint = IM_COL32(255, 255, 255, (color >> IM_COL32_A_SHIFT) & 0xFF);
  list->PushTexture(st.texture.ref());
  list->PrimReserve(int(quads.size()) * 6, int(quads.size()) * 4);
  for (const Quad &q : quads) {
    const Slot &s = *q.slot;
    list->PrimRectUV(q.pos, ImVec2(q.pos.x + s.draw_w, q.pos.y + s.draw_h), ImVec2(float(s.x) * iw, float(s.y) * ih),
                     ImVec2(float(s.x + s.w) * iw, float(s.y + s.h) * ih), s.color ? colour_tint : color);
  }
  list->PopTexture();
}

#endif

} // namespace silta::detail
