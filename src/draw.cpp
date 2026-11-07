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

namespace {
// Points of a rounded rectangle, `seg` + 1 per corner, clockwise from the
// top-left corner, with the outward normal of each point. Rectangles with the
// same `seg` have matching points, so rings can be stitched between them.
constexpr int corner_segments = 8;
constexpr int contour_points = 4 * (corner_segments + 1);

void contour(ImVec2 min, ImVec2 max, float r, ImVec2 *pts, ImVec2 *normals) {
  r = std::max(0.f, std::min(r, std::min(max.x - min.x, max.y - min.y) / 2));
  const ImVec2 centers[4] = {{min.x + r, min.y + r}, {max.x - r, min.y + r}, {max.x - r, max.y - r}, {min.x + r, max.y - r}};
  const float start[4] = {3.14159265f, 4.71238898f, 0.f, 1.57079633f}; // TL, TR, BR, BL
  int n = 0;
  for (int c = 0; c < 4; ++c)
    for (int k = 0; k <= corner_segments; ++k) {
      const float a = start[c] + 1.57079633f * float(k) / float(corner_segments);
      const ImVec2 dir(std::cos(a), std::sin(a));
      if (normals) normals[n] = dir;
      pts[n++] = ImVec2(centers[c].x + dir.x * r, centers[c].y + dir.y * r);
    }
}
} // namespace

void shadow(ImDrawList *list, const ImRect &box, float radius_px, const Shadow &s) {
  if (s.color.a <= 0 || box.GetWidth() <= 0 || box.GetHeight() <= 0) return;
  // CSS box-shadow: the box offset and spread, blurred across [-blur/2,
  // +blur/2] around its edge (a linear ramp approximates the Gaussian), and
  // never drawn inside the element's own box. Built as two rings stitched
  // between matching points of rounded-rectangle contours:
  //   box edge -> inner shadow edge: solid;  inner -> outer edge: fade out.
  const float blur = px(s.blur), spread = px(s.spread);
  const ImVec2 off(px(s.x), px(s.y));
  const float half = blur / 2;
  const float r_shadow = std::max(0.f, radius_px + spread);
  ImVec2 a[contour_points], n[contour_points], inner[contour_points], outer[contour_points];
  contour(box.Min, box.Max, radius_px, a, n);
  contour(ImVec2(box.Min.x + off.x - spread + half, box.Min.y + off.y - spread + half),
          ImVec2(box.Max.x + off.x + spread - half, box.Max.y + off.y + spread - half), std::max(0.f, r_shadow - half),
          inner, nullptr);
  contour(ImVec2(box.Min.x + off.x - spread - half, box.Min.y + off.y - spread - half),
          ImVec2(box.Max.x + off.x + spread + half, box.Max.y + off.y + spread + half), r_shadow + half, outer,
          nullptr);
  // Keep every ring point outside the box (clip the shadow out of the box).
  auto outside = [&](int i, ImVec2 p) {
    const float d = (p.x - a[i].x) * n[i].x + (p.y - a[i].y) * n[i].y;
    return d < 0 ? a[i] : p;
  };
  for (int i = 0; i < contour_points; ++i) inner[i] = outside(i, inner[i]), outer[i] = outside(i, outer[i]);

  const ImU32 solid = s.color.u32();
  const ImU32 clear = s.color.with_alpha(0).u32();
  const ImVec2 uv = list->_Data->TexUvWhitePixel;
  const int quads = contour_points * 2;
  list->PrimReserve(quads * 6, quads * 4);
  for (int i = 0; i < contour_points; ++i) {
    const int j = (i + 1) % contour_points;
    const ImVec2 ring[2][4] = {{a[i], a[j], inner[j], inner[i]}, {inner[i], inner[j], outer[j], outer[i]}};
    const ImU32 cols[2][4] = {{solid, solid, solid, solid}, {solid, solid, clear, clear}};
    for (int r = 0; r < 2; ++r) {
      const ImDrawIdx base = ImDrawIdx(list->_VtxCurrentIdx);
      for (int v = 0; v < 4; ++v) list->PrimWriteVtx(ring[r][v], uv, cols[r][v]);
      list->PrimWriteIdx(base), list->PrimWriteIdx(ImDrawIdx(base + 1)), list->PrimWriteIdx(ImDrawIdx(base + 2));
      list->PrimWriteIdx(base), list->PrimWriteIdx(ImDrawIdx(base + 2)), list->PrimWriteIdx(ImDrawIdx(base + 3));
    }
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
