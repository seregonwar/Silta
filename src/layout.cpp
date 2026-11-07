#include "silta/layout.hpp"
#include "silta/context.hpp"
#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <vector>

namespace silta {
namespace {
// What a container measured last frame.
struct Memory {
  ImVec2 outer{0, 0};
  float content_main = 0;  // children + gaps on the main axis
  float content_cross = 0; // largest child on the cross axis
  float fixed_main = 0;    // children that do not grow
  int grow_count = 0;
  int child_count = 0;
  int last_frame = 0;
};

struct Live {
  ImGuiID id = 0;
  FlexOptions options;
  Memory *memory = nullptr;
  bool top_level = false;
  bool grow = false;     // this container grows in its parent
  bool fit_width = false, fit_height = false; // sized by content
  ImRect outer;          // outer box (size may be last frame's)
  ImVec2 origin;         // content box top-left
  ImVec2 inner;          // content box size
  float gap = 0;
  float between = 0;     // extra gap from justify-between
  float cursor = 0;      // next child position on the main axis
  int child_seq = 0;     // identity for nested containers
  // Measured this frame.
  float content_main = 0, content_cross = 0, fixed_main = 0;
  int grow_count = 0, child_count = 0;
};

std::unordered_map<ImGuiID, Memory> memories;
std::unordered_map<ImGuiID, int> root_sequence;
std::vector<Live> stack;
std::vector<std::vector<Live>> saved_stacks; // outer layouts while isolated
bool layout_changed = false;

float snap(float v) { return std::floor(v + 0.5f); }
bool is_row(const Live &f) { return f.options.axis == Axis::row; }
float main_of(const Live &f, ImVec2 v) { return is_row(f) ? v.x : v.y; }
float cross_of(const Live &f, ImVec2 v) { return is_row(f) ? v.y : v.x; }

float inner_cross(const Live &f) {
  const float c = cross_of(f, f.inner);
  return c > 0 ? c : f.memory->content_cross;
}

// Where the next child goes, given the size it expects to have.
ImVec2 next_position(const Live &f, ImVec2 size) {
  const float free = inner_cross(f) - cross_of(f, size);
  float cross = 0;
  if (f.options.align == Align::center) cross = std::max(0.f, free / 2);
  else if (f.options.align == Align::end) cross = std::max(0.f, free);
  const float main = f.cursor;
  return is_row(f) ? ImVec2(snap(f.origin.x + main), snap(f.origin.y + cross))
                   : ImVec2(snap(f.origin.x + cross), snap(f.origin.y + main));
}

void advance(Live &f, ImVec2 size, bool grow) {
  const float m = main_of(f, size), c = cross_of(f, size);
  if (f.child_count > 0) f.content_main += f.gap;
  f.content_main += m;
  f.content_cross = std::max(f.content_cross, c);
  if (grow) ++f.grow_count;
  else f.fixed_main += m;
  ++f.child_count;
  f.cursor += m + f.gap + f.between;
}

bool parent_stretches() {
  return !stack.empty() && stack.back().options.align == Align::stretch;
}
} // namespace

namespace layout {

float fill_width() {
  if (stack.empty()) return std::max(0.f, ImGui::GetContentRegionAvail().x);
  const Live &f = stack.back();
  if (!is_row(f)) return inner_cross(f);
  const Memory &m = *f.memory;
  const float gaps = f.gap * float(std::max(0, m.child_count - 1));
  const float share = (f.inner.x - m.fixed_main - gaps) / float(std::max(1, m.grow_count));
  return std::max(0.f, share);
}

float fill_height() {
  if (stack.empty()) return std::max(0.f, ImGui::GetContentRegionAvail().y);
  const Live &f = stack.back();
  if (is_row(f)) return inner_cross(f);
  const Memory &m = *f.memory;
  const float gaps = f.gap * float(std::max(0, m.child_count - 1));
  const float share = (f.inner.y - m.fixed_main - gaps) / float(std::max(1, m.grow_count));
  return std::max(0.f, share);
}

float resolve_width(float points, float content_px) {
  if (points > 0) return px(points);
  if (points == fill) return fill_width();
  // align-items: stretch makes children of a column as wide as the column.
  if (parent_stretches() && !is_row(stack.back())) return fill_width();
  return content_px;
}

float resolve_height(float points, float content_px) {
  if (points > 0) return px(points);
  if (points == fill) return fill_height();
  if (parent_stretches() && is_row(stack.back())) return fill_height();
  return content_px;
}

ImRect allocate(ImVec2 size, bool grow) {
  size = ImVec2(snap(size.x), snap(size.y));
  if (stack.empty()) {
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::ItemSize(size);
    return {pos, ImVec2(pos.x + size.x, pos.y + size.y)};
  }
  Live &f = stack.back();
  const ImVec2 pos = next_position(f, size);
  advance(f, size, grow);
  return {pos, ImVec2(pos.x + size.x, pos.y + size.y)};
}

Isolate::Isolate() {
  saved_stacks.push_back(std::move(stack));
  stack.clear();
}

Isolate::~Isolate() {
  IM_ASSERT(stack.empty() && "silta: a Flex scope was not closed inside an isolated layout");
  stack = std::move(saved_stacks.back());
  saved_stacks.pop_back();
}

bool at_least(Breakpoint breakpoint) {
  static constexpr float widths[] = {640, 768, 1024, 1280};
  const float w = ImGui::GetMainViewport()->Size.x / scale();
  return w >= widths[int(breakpoint)];
}

void begin_frame() {
  layout_changed = false;
  root_sequence.clear();
  stack.clear();
  saved_stacks.clear();
}

void end_frame() {
  IM_ASSERT(stack.empty() && "silta: a Flex scope was not closed");
  const int frame = ImGui::GetFrameCount();
  if (frame % 60 != 0) return;
  for (auto it = memories.begin(); it != memories.end();)
    it = frame - it->second.last_frame > 120 ? memories.erase(it) : std::next(it);
}

bool settling() { return layout_changed; }

} // namespace layout

Flex::Flex(const FlexOptions &options) {
  Live f;
  f.options = options;
  f.top_level = stack.empty();

  // Identity: explicit, or the position among the parent's containers.
  if (options.id) {
    f.id = ImGui::GetID(options.id);
  } else {
    const ImGuiID seed = f.top_level ? ImGui::GetID("##silta.flex") : stack.back().id;
    int seq = f.top_level ? root_sequence[seed]++ : stack.back().child_seq++;
    f.id = ImHashData(&seq, sizeof seq, seed);
  }
  f.memory = &memories[f.id];
  f.memory->last_frame = ImGui::GetFrameCount();

  const Edges &p = options.padding;
  const float pl = px(p.left), pr = px(p.right), pt = px(p.top), pb = px(p.bottom);
  f.gap = px(options.gap);

  // Outer size: fixed and fill resolve now; fit-content comes from last frame.
  ImVec2 size(layout::resolve_width(options.width, f.memory->outer.x),
              layout::resolve_height(options.height, f.memory->outer.y));
  f.grow = !f.top_level && ((is_row(stack.back()) && options.width == fill) ||
                            (!is_row(stack.back()) && options.height == fill));
  const bool stretched_x = parent_stretches() && !is_row(stack.back());
  const bool stretched_y = parent_stretches() && is_row(stack.back());
  f.fit_width = options.width == 0 && !stretched_x;
  f.fit_height = options.height == 0 && !stretched_y;
  size = ImVec2(snap(size.x), snap(size.y));

  const ImVec2 pos = f.top_level ? ImGui::GetCursorScreenPos() : next_position(stack.back(), size);
  f.outer = ImRect(pos, ImVec2(pos.x + size.x, pos.y + size.y));
  f.origin = ImVec2(pos.x + pl, pos.y + pt);
  f.inner = ImVec2(std::max(0.f, size.x - pl - pr), std::max(0.f, size.y - pt - pb));

  // Justify with last frame's content size.
  const Memory &m = *f.memory;
  const float free = std::max(0.f, main_of(f, f.inner) - m.content_main);
  switch (options.justify) {
  case Justify::start: break;
  case Justify::center: f.cursor = free / 2; break;
  case Justify::end: f.cursor = free; break;
  case Justify::between: f.between = m.child_count > 1 ? free / float(m.child_count - 1) : 0; break;
  }

  id_ = f.id;
  stack.push_back(f);
  ImGui::PushOverrideID(f.id);
}

Flex::~Flex() {
  ImGui::PopID();
  Live f = stack.back();
  stack.pop_back();
  Memory &m = *f.memory;

  const Edges &p = f.options.padding;
  const float pad_x = px(p.left) + px(p.right), pad_y = px(p.top) + px(p.bottom);
  const float content_w = is_row(f) ? f.content_main : f.content_cross;
  const float content_h = is_row(f) ? f.content_cross : f.content_main;
  // Fit-content sides take the measured size; the others keep what they got.
  ImVec2 size = f.outer.GetSize();
  if (f.fit_width) size.x = snap(content_w + pad_x);
  if (f.fit_height) size.y = snap(content_h + pad_y);

  auto differs = [](float a, float b) { return std::fabs(a - b) > 0.5f; };
  if (differs(size.x, m.outer.x) || differs(size.y, m.outer.y) || differs(f.content_main, m.content_main) ||
      differs(f.content_cross, m.content_cross) || differs(f.fixed_main, m.fixed_main) ||
      f.grow_count != m.grow_count || f.child_count != m.child_count)
    layout_changed = true;
  m.outer = size;
  m.content_main = f.content_main;
  m.content_cross = f.content_cross;
  m.fixed_main = f.fixed_main;
  m.grow_count = f.grow_count;
  m.child_count = f.child_count;

  if (f.top_level) {
    ImGui::SetCursorScreenPos(f.outer.Min);
    ImGui::ItemSize(size);
  } else {
    advance(stack.back(), size, f.grow);
  }
}

ImRect Flex::rect() const {
  for (const Live &f : stack)
    if (f.id == id_) return f.outer;
  return {};
}

void Spacer(float points) {
  const float v = px(points);
  const bool row = !stack.empty() && is_row(stack.back());
  layout::allocate(row ? ImVec2(v, 0) : ImVec2(0, v));
}

} // namespace silta
