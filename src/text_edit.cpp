#include "text_edit.hpp"
#include "shaper.hpp"
#include "imgui_internal.h"
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstring>

namespace silta::detail {
namespace {
void add_line(const char *text, std::size_t begin, std::size_t end, int weight, float font_size, bool rtl,
              float content_width, EditLayout &out) {
  ShapedLineInfo info;
  if (!shaped_line_info(text + begin, text + end, weight, font_size, rtl, info)) return;
  EditLine line;
  line.begin = begin;
  line.end = end;
  line.width = info.width;
  line.clusters.reserve(info.clusters.size());
  for (const ShapedCluster &c : info.clusters)
    line.clusters.push_back({begin + c.begin, begin + c.end, c.x0, c.x1, c.level});
  line.carets.reserve(info.carets.size());
  for (const ShapedCaret &c : info.carets) line.carets.push_back({begin + c.offset, c.x, c.level});
  if (line.carets.empty()) line.carets.push_back({begin, rtl ? line.width : 0.f, rtl ? 1 : 0});
  line.origin = rtl ? content_width - line.width : 0.f;
  out.width = std::max(out.width, line.width);
  out.lines.push_back(std::move(line));
}

float distance(float a, float b) { return std::fabs(a - b); }

// Caret stops of a line in visual order, one per distinct x.
std::vector<CaretPosition> visual_stops(const EditLayout &layout, int index,
                                        const CaretPosition *current = nullptr) {
  std::vector<CaretPosition> stops;
  if (index < 0 || index >= int(layout.lines.size())) return stops;
  const EditLine &line = layout.lines[size_t(index)];
  stops.reserve(line.carets.size());
  for (const EditCaret &c : line.carets) {
    const float x = line.origin + c.x;
    bool duplicate = false;
    for (const CaretPosition &other : stops)
      if (distance(other.x, x) < 0.25f) duplicate = true;
    if (!duplicate) stops.push_back({index, c.offset, x, c.level});
  }
  std::sort(stops.begin(), stops.end(), [](const CaretPosition &a, const CaretPosition &b) {
    if (distance(a.x, b.x) >= 0.25f) return a.x < b.x;
    return a.offset < b.offset;
  });
  (void)current;
  return stops;
}

CaretPosition nearest_by_offset(const EditLayout &layout, std::size_t offset, float hint_x, int line_hint,
                                bool has_hint) {
  CaretPosition best;
  bool found = false;
  float best_distance = FLT_MAX;
  for (int pass = 0; pass < (line_hint >= 0 ? 2 : 1); ++pass) {
    for (int i = 0; i < int(layout.lines.size()); ++i) {
      if (pass == 0 && line_hint >= 0 && i != line_hint) continue;
      const EditLine &line = layout.lines[size_t(i)];
      for (const EditCaret &c : line.carets) {
        if (c.offset != offset) continue;
        const float d = has_hint ? distance(line.origin + c.x, hint_x) : 0.f;
        if (!found || d < best_distance - 0.01f) {
          best = {i, c.offset, line.origin + c.x, c.level};
          best_distance = d;
          found = true;
        }
      }
    }
    if (found) break;
  }
  if (found) return best;
  // No caret at this offset (it can only be inside a ligature or cluster):
  // snap to the closest cluster edge.
  std::size_t best_delta = std::size_t(-1);
  for (int i = 0; i < int(layout.lines.size()); ++i) {
    const EditLine &line = layout.lines[size_t(i)];
    for (const EditCaret &c : line.carets) {
      const std::size_t delta = c.offset > offset ? c.offset - offset : offset - c.offset;
      if (delta < best_delta) {
        best = {i, c.offset, line.origin + c.x, c.level};
        best_delta = delta;
      }
    }
  }
  return best;
}
} // namespace

bool shaped_editing_available() { return shaping_available(); }

bool text_needs_shaped_editor(const char *text, bool rtl) {
  if (!shaped_editing_available()) return false;
  if (rtl) return true;
  if (!text) return false;
  return needs_shaping(text, text + std::strlen(text));
}

void layout_edit_text(const char *text, std::size_t size, int weight, float font_size, bool rtl, bool wrap,
                      float wrap_width, float content_width, EditLayout &out) {
  out = {};
  out.rtl = rtl;
  out.line_height = font_size;
  const float width = content_width > 0 ? content_width : (wrap_width > 0 ? wrap_width : 0.f);
  if (size == 0) {
    add_line(text, 0, 0, weight, font_size, rtl, width, out);
    out.height = out.line_height;
    out.content_width = width;
    return;
  }

  std::size_t pos = 0;
  for (;;) {
    const char *newline = static_cast<const char *>(std::memchr(text + pos, '\n', size - pos));
    const std::size_t para_end = newline ? std::size_t(newline - text) : size;
    if (para_end == pos) {
      add_line(text, pos, pos, weight, font_size, rtl, width, out);
    } else {
      std::size_t start = pos;
      while (start < para_end) {
        std::size_t end = para_end;
        if (wrap && wrap_width > 0) {
          const char *br = shaped_wrap(text + start, text + para_end, weight, font_size, wrap_width, rtl);
          end = std::size_t(br - text);
          if (end <= start) end = para_end; // always make progress on bad metrics
        }
        add_line(text, start, end, weight, font_size, rtl, width, out);
        start = end;
      }
    }
    if (!newline) break;
    pos = para_end + 1;
    if (pos == size) { // text ends with a newline: the caret has an empty last line
      add_line(text, pos, pos, weight, font_size, rtl, width, out);
      break;
    }
  }
  out.height = float(out.lines.size()) * out.line_height;
  out.content_width = width > 0 ? width : out.width;
  if (width <= 0) {
    for (EditLine &line : out.lines) line.origin = rtl ? out.content_width - line.width : 0.f;
  }
}

float edit_text_height(const char *text, std::size_t size, int weight, float font_size, bool rtl, bool wrap,
                       float wrap_width) {
  EditLayout layout;
  layout_edit_text(text, size, weight, font_size, rtl, wrap, wrap_width, wrap_width, layout);
  return layout.height;
}

CaretPosition caret_at(const EditLayout &layout, std::size_t offset, float hint_x, int line_hint) {
  if (layout.lines.empty()) return {};
  return nearest_by_offset(layout, offset, hint_x, line_hint, true);
}

CaretPosition caret_at_point(const EditLayout &layout, float x, float y) {
  if (layout.lines.empty()) return {};
  const float lh = std::max(1.f, layout.line_height);
  int index = int(std::floor(y / lh));
  index = std::clamp(index, 0, int(layout.lines.size()) - 1);
  const EditLine &line = layout.lines[size_t(index)];
  CaretPosition best;
  bool found = false;
  float best_distance = FLT_MAX;
  for (const EditCaret &c : line.carets) {
    const float cx = line.origin + c.x;
    const float d = distance(cx, x);
    if (!found || d < best_distance) {
      best = {index, c.offset, cx, c.level};
      best_distance = d;
      found = true;
    }
  }
  if (!found) {
    const bool left = layout.rtl;
    best = {index, left ? line.end : line.begin, line.origin + (left ? 0.f : line.width), layout.rtl ? 1 : 0};
  }
  return best;
}

CaretPosition line_edge(const EditLayout &layout, int index, bool left) {
  if (layout.lines.empty()) return {};
  index = std::clamp(index, 0, int(layout.lines.size()) - 1);
  const EditLine &line = layout.lines[size_t(index)];
  CaretPosition best;
  bool found = false;
  float best_x = left ? FLT_MAX : -FLT_MAX;
  for (const EditCaret &c : line.carets) {
    const float x = line.origin + c.x;
    const bool better = !found || (left ? x < best_x - 0.01f : x > best_x + 0.01f);
    const bool same = found && distance(x, best_x) < 0.01f;
    // At an edge shared by two runs, pick the paragraph's logical end/start.
    const bool tie = same && ((left == layout.rtl) ? c.offset > best.offset : c.offset < best.offset);
    if (better || tie) {
      best = {index, c.offset, x, c.level};
      best_x = x;
      found = true;
    }
  }
  if (!found) {
    const std::size_t offset = left ? (layout.rtl ? line.end : line.begin) : (layout.rtl ? line.begin : line.end);
    best = {index, offset, line.origin + (left ? 0.f : line.width), layout.rtl ? 1 : 0};
  }
  return best;
}

CaretPosition move_caret(const EditLayout &layout, const CaretPosition &from, int dir) {
  if (layout.lines.empty() || dir == 0) return from;
  const int index = std::clamp(from.line, 0, int(layout.lines.size()) - 1);
  std::vector<CaretPosition> stops = visual_stops(layout, index);
  int current = -1;
  float best = FLT_MAX;
  for (int i = 0; i < int(stops.size()); ++i) {
    const float d = distance(stops[size_t(i)].x, from.x);
    if (stops[size_t(i)].offset == from.offset && d < best) {
      current = i;
      best = d;
    }
  }
  if (current < 0)
    for (int i = 0; i < int(stops.size()); ++i) {
      const float d = distance(stops[size_t(i)].x, from.x);
      if (d < best) {
        current = i;
        best = d;
      }
    }
  if (current >= 0) {
    if (dir > 0) {
      for (int i = current + 1; i < int(stops.size()); ++i)
        if (stops[size_t(i)].x > stops[size_t(current)].x + 0.5f) return stops[size_t(i)];
    } else {
      for (int i = current - 1; i >= 0; --i)
        if (stops[size_t(i)].x < stops[size_t(current)].x - 0.5f) return stops[size_t(i)];
    }
  }
  // At the line's edge, continue on the neighbouring line. RTL lines read
  // right-to-left, so "left" wraps forward and "right" wraps backward.
  int target = -1;
  bool left_edge = false;
  if (layout.rtl) {
    if (dir > 0) {
      target = index - 1;
      left_edge = true;
    } else {
      target = index + 1;
      left_edge = false;
    }
  } else {
    if (dir > 0) {
      target = index + 1;
      left_edge = true;
    } else {
      target = index - 1;
      left_edge = false;
    }
  }
  if (target >= 0 && target < int(layout.lines.size())) return line_edge(layout, target, left_edge);
  return from;
}

CaretPosition move_line(const EditLayout &layout, const CaretPosition &from, int dir) {
  if (layout.lines.empty() || dir == 0) return from;
  const int target = std::clamp(from.line + dir, 0, int(layout.lines.size()) - 1);
  if (target == from.line) return from;
  const EditLine &line = layout.lines[size_t(target)];
  CaretPosition best;
  bool found = false;
  float best_distance = FLT_MAX;
  for (const EditCaret &c : line.carets) {
    const float x = line.origin + c.x;
    const float d = distance(x, from.x);
    if (!found || d < best_distance) {
      best = {target, c.offset, x, c.level};
      best_distance = d;
      found = true;
    }
  }
  if (!found) {
    const bool left = layout.rtl;
    best = {target, left ? line.end : line.begin, line.origin + (left ? 0.f : line.width), layout.rtl ? 1 : 0};
  }
  return best;
}

std::size_t next_byte(const char *text, std::size_t size, std::size_t pos) {
  if (pos >= size) return size;
  unsigned int codepoint = 0;
  const int n = ImTextCharFromUtf8(&codepoint, text + pos, text + size);
  return pos + std::size_t(std::max(1, n));
}

std::size_t prev_byte(const char *text, std::size_t size, std::size_t pos) {
  if (pos == 0) return 0;
  std::size_t p = std::min(pos, size);
  --p;
  while (p > 0 && (static_cast<unsigned char>(text[p]) & 0xC0) == 0x80) --p;
  return p;
}

std::string encode_utf8(unsigned int codepoint) {
  char buffer[5] = {};
  const int n = ImTextCharToUtf8(buffer, codepoint);
  return std::string(buffer, std::size_t(std::max(0, n)));
}

std::string selection_text(const std::string &text, std::size_t cursor, std::size_t anchor) {
  const std::size_t a = std::min(cursor, anchor), b = std::max(cursor, anchor);
  if (a >= text.size()) return {};
  return text.substr(a, std::min(b, text.size()) - a);
}

void replace_selection(std::string &text, std::size_t &cursor, std::size_t &anchor, const std::string &insert) {
  const std::size_t a = std::min(cursor, anchor), b = std::max(cursor, anchor);
  text.erase(a, b - a);
  text.insert(a, insert);
  cursor = anchor = a + insert.size();
}

void erase_selection(std::string &text, std::size_t &cursor, std::size_t &anchor) {
  const std::size_t a = std::min(cursor, anchor), b = std::max(cursor, anchor);
  if (a == b) return;
  text.erase(a, b - a);
  cursor = anchor = a;
}

bool visual_delete_range(const char *text, std::size_t size, const EditLayout &layout,
                         const CaretPosition &caret, int dir, std::size_t &begin, std::size_t &end) {
  begin = end = caret.offset;
  const CaretPosition neighbour = move_caret(layout, caret, dir);
  std::size_t target = neighbour.offset;
  if (target == caret.offset) {
    if (dir < 0) target = prev_byte(text, size, caret.offset);
    else target = next_byte(text, size, caret.offset);
  }
  begin = std::min(target, caret.offset);
  end = std::max(target, caret.offset);
  return begin != end;
}

std::string sanitize_paste(const std::string &input, bool multiline) {
  std::string out;
  out.reserve(input.size());
  for (std::size_t i = 0; i < input.size(); ++i) {
    const char c = input[i];
    if (c == '\r') {
      if (i + 1 < input.size() && input[i + 1] == '\n') ++i;
      if (multiline) out += '\n';
    } else if (c == '\n') {
      if (multiline) out += '\n';
    } else if (c == '\t') {
      out += multiline ? '\t' : ' ';
    } else {
      out += c;
    }
  }
  return out;
}

void ime_begin(ImeEdit &ime, const std::string &preview, std::size_t cursor, std::size_t anchor) {
  ime.active = true;
  ime.preview = preview;
  ime.cursor = cursor;
  ime.anchor = anchor;
}

void ime_update(ImeEdit &ime, const std::string &preview) {
  if (ime.active) ime.preview = preview;
}

bool ime_commit(std::string &text, ImeEdit &ime, std::size_t &cursor, std::size_t &anchor) {
  if (!ime.active) return false;
  const bool changed = !ime.preview.empty();
  if (changed) {
    // The editor's offsets are already clamped to the text, but a host can
    // begin a composition between frames while the value changes underneath.
    ime.cursor = std::min(ime.cursor, text.size());
    ime.anchor = std::min(ime.anchor, text.size());
    replace_selection(text, ime.cursor, ime.anchor, ime.preview);
    cursor = ime.cursor;
    anchor = ime.anchor;
  }
  ime = {};
  return changed;
}

void ime_cancel(ImeEdit &ime) { ime = {}; }

std::string ime_preview_text(const std::string &text, const ImeEdit &ime) {
  if (!ime.active) return text;
  const std::size_t a = std::min(ime.cursor, ime.anchor);
  const std::size_t b = std::max(ime.cursor, ime.anchor);
  const std::size_t begin = std::min(a, text.size());
  const std::size_t end = std::min(b, text.size());
  std::string out;
  out.reserve(begin + ime.preview.size() + (text.size() - end));
  out.append(text, 0, begin);
  out += ime.preview;
  out.append(text, end, std::string::npos);
  return out;
}

std::size_t ime_preview_caret(const ImeEdit &ime) {
  return std::min(ime.cursor, ime.anchor) + ime.preview.size();
}

} // namespace silta::detail
