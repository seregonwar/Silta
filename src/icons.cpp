#include "silta/icons.hpp"
#include "silta/context.hpp"
#include <cmath>
#include <algorithm>
#include <initializer_list>

namespace silta {
namespace {
constexpr float pi = 3.14159265358979f;

struct Pen {
  ImDrawList *list;
  ImVec2 min;
  float s; // pixels per grid unit
  ImU32 col;
  float w;

  ImVec2 p(float x, float y) const { return {min.x + x * s, min.y + y * s}; }
  void cap(ImVec2 at) const { list->AddCircleFilled(at, w / 2, col, 8); }
  void line(std::initializer_list<ImVec2> pts) const {
    for (ImVec2 q : pts) list->PathLineTo(p(q.x, q.y));
    list->PathStroke(col, w);
    cap(p(pts.begin()->x, pts.begin()->y));
    cap(p((pts.end() - 1)->x, (pts.end() - 1)->y));
    for (auto it = pts.begin() + 1; it + 1 < pts.end(); ++it) cap(p(it->x, it->y));
  }
  void circle(float cx, float cy, float r) const { list->AddCircle(p(cx, cy), r * s, col, 0, w); }
  void arc(float cx, float cy, float r, float a0, float a1) const {
    list->PathArcTo(p(cx, cy), r * s, a0, a1);
    list->PathStroke(col, w);
  }
};
} // namespace

void draw_icon(ImDrawList *list, Icon icon, ImVec2 min, float size_px, Color color) {
  if (icon == Icon::none || color.a <= 0) return;
  const Pen pen{list, min, size_px / 24.f, color.u32(), std::max(1.f, size_px / 12.f)};
  switch (icon) {
  case Icon::none: break;
  case Icon::check: pen.line({{20, 6}, {9, 17}, {4, 12}}); break;
  case Icon::x:
    pen.line({{18, 6}, {6, 18}});
    pen.line({{6, 6}, {18, 18}});
    break;
  case Icon::plus:
    pen.line({{5, 12}, {19, 12}});
    pen.line({{12, 5}, {12, 19}});
    break;
  case Icon::minus: pen.line({{5, 12}, {19, 12}}); break;
  case Icon::chevron_right: pen.line({{9, 18}, {15, 12}, {9, 6}}); break;
  case Icon::chevron_left: pen.line({{15, 18}, {9, 12}, {15, 6}}); break;
  case Icon::chevron_down: pen.line({{6, 9}, {12, 15}, {18, 9}}); break;
  case Icon::chevron_up: pen.line({{18, 15}, {12, 9}, {6, 15}}); break;
  case Icon::arrow_right:
    pen.line({{5, 12}, {19, 12}});
    pen.line({{12, 5}, {19, 12}, {12, 19}});
    break;
  case Icon::mail:
    list->AddRect(pen.p(2, 4), pen.p(22, 20), pen.col, 2 * pen.s, pen.w);
    pen.line({{22, 7}, {13, 12.7f}, {11, 12.7f}, {2, 7}});
    break;
  case Icon::search:
    pen.circle(11, 11, 8);
    pen.line({{21, 21}, {16.65f, 16.65f}});
    break;
  case Icon::loader: {
    const float spin = float(std::fmod(ImGui::GetTime(), 1.0)) * 2 * pi;
    pen.arc(12, 12, 9, spin, spin + 1.6f * pi);
    request_frames(1);
    break;
  }
  case Icon::bell:
    pen.arc(12, 8, 6, pi, 2 * pi);
    pen.line({{6, 8}, {6, 12}, {3.5f, 16}, {3.5f, 17}, {20.5f, 17}, {20.5f, 16}, {18, 12}, {18, 8}});
    pen.arc(12, 20, 2, 0.15f * pi, 0.85f * pi);
    break;
  case Icon::settings: {
    // Eight-tooth gear around a hub.
    const int teeth = 8;
    for (int i = 0; i <= teeth * 4; ++i) {
      const float a = float(i) / float(teeth * 4) * 2 * pi;
      const float r = (i % 4 == 1 || i % 4 == 2) ? 10.f : 7.5f;
      list->PathLineTo(pen.p(12 + std::cos(a) * r, 12 + std::sin(a) * r));
    }
    list->PathStroke(pen.col, pen.w, ImDrawFlags_Closed);
    pen.circle(12, 12, 3);
    break;
  }
  case Icon::user:
    pen.circle(12, 7, 4);
    list->PathLineTo(pen.p(19, 21));
    list->PathLineTo(pen.p(19, 19));
    list->PathArcTo(pen.p(15, 19), 4 * pen.s, 0, -pi / 2);
    list->PathLineTo(pen.p(9, 15));
    list->PathArcTo(pen.p(9, 19), 4 * pen.s, -pi / 2, -pi);
    list->PathLineTo(pen.p(5, 21));
    list->PathStroke(pen.col, pen.w);
    break;
  }
}

} // namespace silta
