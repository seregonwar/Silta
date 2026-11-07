#include "silta/draw.hpp"
#include "silta/context.hpp"
#include <algorithm>
#include <cmath>

namespace silta::draw {

ShadowStack shadow_xs() { return {{{0, 1, 2, 0, black.with_alpha(0.05f)}}, 1}; }
ShadowStack shadow_sm() {
  return {{{0, 1, 3, 0, black.with_alpha(0.10f)}, {0, 1, 2, -1, black.with_alpha(0.10f)}}, 2};
}
ShadowStack shadow_md() {
  return {{{0, 4, 6, -1, black.with_alpha(0.10f)}, {0, 2, 4, -2, black.with_alpha(0.10f)}}, 2};
}
ShadowStack shadow_lg() {
  return {{{0, 10, 15, -3, black.with_alpha(0.10f)}, {0, 4, 6, -4, black.with_alpha(0.10f)}}, 2};
}

void shadow(ImDrawList *list, const ImRect &box, float radius_px, const Shadow &s) {
  if (s.color.a <= 0) return;
  // A Gaussian blur of a rounded box, approximated by stacked translucent
  // boxes whose edges spread over [-blur/2, +blur/2] around the shadow edge.
  const float blur = px(s.blur), spread = px(s.spread);
  const ImVec2 off(px(s.x), px(s.y));
  const int steps = std::clamp(int(std::ceil(blur)), 1, 16);
  const float layer_alpha = 1.f - std::pow(1.f - s.color.a, 1.f / float(steps));
  const ImU32 col = s.color.with_alpha(layer_alpha).u32();
  for (int i = 0; i < steps; ++i) {
    const float t = steps == 1 ? 0.5f : float(i) / float(steps - 1);
    const float grow = spread + blur * (0.5f - t);
    const ImVec2 a(box.Min.x + off.x - grow, box.Min.y + off.y - grow);
    const ImVec2 b(box.Max.x + off.x + grow, box.Max.y + off.y + grow);
    if (b.x <= a.x || b.y <= a.y) continue;
    list->AddRectFilled(a, b, col, std::max(0.f, radius_px + grow));
  }
}

void shadow(ImDrawList *list, const ImRect &box, float radius_px, const ShadowStack &s) {
  for (int i = 0; i < s.count; ++i) shadow(list, box, radius_px, s.layers[i]);
}

void fill(ImDrawList *list, const ImRect &box, Color color, float radius_px, ImDrawFlags flags) {
  if (color.a <= 0) return;
  list->AddRectFilled(box.Min, box.Max, color.u32(), radius_px, flags);
}

void border(ImDrawList *list, const ImRect &box, Color color, float radius_px, float width_px) {
  if (color.a <= 0 || width_px <= 0) return;
  // ImGui strokes on the path; inset by half the width so the stroke lies
  // inside the box like a CSS border.
  const float h = width_px / 2;
  list->AddRect(ImVec2(box.Min.x + h, box.Min.y + h), ImVec2(box.Max.x - h, box.Max.y - h), color.u32(),
                std::max(0.f, radius_px - h), width_px);
}

void focus_ring(ImDrawList *list, const ImRect &box, float radius_px, Color ring) {
  const float w = px(3), h = w / 2;
  list->AddRect(ImVec2(box.Min.x - h, box.Min.y - h), ImVec2(box.Max.x + h, box.Max.y + h),
                ring.fade(0.5f).u32(), radius_px + h, w);
}

} // namespace silta::draw
