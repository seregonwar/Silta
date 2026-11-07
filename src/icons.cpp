#include "silta/icons.hpp"
#include "silta/context.hpp"
#include <algorithm>
#include <cmath>
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
  // Open polyline with round caps and joins.
  void line(std::initializer_list<ImVec2> pts) const {
    for (ImVec2 q : pts) list->PathLineTo(p(q.x, q.y));
    list->PathStroke(col, w);
    for (ImVec2 q : pts) cap(p(q.x, q.y));
  }
  // Closed polygon, stroked or filled.
  void poly(std::initializer_list<ImVec2> pts, bool fill) const {
    for (ImVec2 q : pts) list->PathLineTo(p(q.x, q.y));
    if (fill) {
      list->PathFillConcave(col);
    } else {
      list->PathStroke(col, w, ImDrawFlags_Closed);
      for (ImVec2 q : pts) cap(p(q.x, q.y));
    }
  }
  void rect(float x0, float y0, float x1, float y1, float r, bool fill = false) const {
    if (fill) list->AddRectFilled(p(x0, y0), p(x1, y1), col, r * s);
    else list->AddRect(p(x0, y0), p(x1, y1), col, r * s, w);
  }
  void circle(float cx, float cy, float r) const { list->AddCircle(p(cx, cy), r * s, col, 0, w); }
  void disc(float cx, float cy, float r) const { list->AddCircleFilled(p(cx, cy), r * s, col, 0); }
  void arc(float cx, float cy, float r, float a0, float a1) const {
    list->PathArcTo(p(cx, cy), r * s, a0, a1);
    list->PathStroke(col, w);
  }
  // Open path (lines and arcs) drawn as one stroke.
  void to(float x, float y) const { list->PathLineTo(p(x, y)); }
  void arc_to(float cx, float cy, float r, float a0, float a1) const { list->PathArcTo(p(cx, cy), r * s, a0, a1); }
  void stroke(bool closed = false) const { list->PathStroke(col, w, closed ? ImDrawFlags_Closed : 0); }
};
} // namespace

void draw_icon(ImDrawList *list, Icon icon, ImVec2 min, float size_px, Color color, bool filled) {
  if (icon == Icon::none || color.a <= 0) return;
  const Pen pen{list, min, size_px / 24.f, color.u32(), std::max(1.f, size_px / 12.f)};
  switch (icon) {
  case Icon::none: break;
  // ---- Interface
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
  case Icon::chevrons_up_down:
    pen.line({{7, 15}, {12, 20}, {17, 15}});
    pen.line({{7, 9}, {12, 4}, {17, 9}});
    break;
  case Icon::arrow_right:
    pen.line({{5, 12}, {19, 12}});
    pen.line({{12, 5}, {19, 12}, {12, 19}});
    break;
  case Icon::arrow_left:
    pen.line({{19, 12}, {5, 12}});
    pen.line({{12, 19}, {5, 12}, {12, 5}});
    break;
  case Icon::more_horizontal:
    pen.disc(5, 12, 1.6f), pen.disc(12, 12, 1.6f), pen.disc(19, 12, 1.6f);
    break;
  case Icon::more_vertical:
    pen.disc(12, 5, 1.6f), pen.disc(12, 12, 1.6f), pen.disc(12, 19, 1.6f);
    break;
  case Icon::grip_vertical:
    for (float y : {5.f, 12.f, 19.f}) pen.disc(9, y, 1.4f), pen.disc(15, y, 1.4f);
    break;
  case Icon::search:
    pen.circle(11, 11, 8);
    pen.line({{21, 21}, {16.65f, 16.65f}});
    break;
  case Icon::settings: {
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
    pen.to(19, 21), pen.to(19, 19);
    pen.arc_to(15, 19, 4, 0, -pi / 2);
    pen.to(9, 15);
    pen.arc_to(9, 19, 4, -pi / 2, -pi);
    pen.to(5, 21);
    pen.stroke();
    break;
  case Icon::bell:
    pen.arc(12, 8, 6, pi, 2 * pi);
    pen.line({{6, 8}, {6, 12}, {3.5f, 16}, {3.5f, 17}, {20.5f, 17}, {20.5f, 16}, {18, 12}, {18, 8}});
    pen.arc(12, 20, 2, 0.15f * pi, 0.85f * pi);
    break;
  case Icon::info:
    pen.circle(12, 12, 10);
    pen.line({{12, 16}, {12, 12}});
    pen.disc(12, 8, 1.1f);
    break;
  case Icon::alert:
    pen.circle(12, 12, 10);
    pen.line({{12, 8}, {12, 12}});
    pen.disc(12, 16, 1.1f);
    break;
  case Icon::sun:
    pen.circle(12, 12, 4);
    for (int i = 0; i < 8; ++i) {
      const float a = float(i) * pi / 4, c = std::cos(a), sn = std::sin(a);
      pen.line({{12 + c * 7.5f, 12 + sn * 7.5f}, {12 + c * 9.5f, 12 + sn * 9.5f}});
    }
    break;
  case Icon::moon:
    pen.arc_to(12, 12, 9, 0, 1.5f * pi); // right, bottom, left, top
    pen.arc_to(21, 3, 9, pi, 0.5f * pi); // inner curve back to the right
    pen.stroke(true);
    break;
  case Icon::maximize:
    pen.line({{8, 3}, {3, 3}, {3, 8}});
    pen.line({{21, 8}, {21, 3}, {16, 3}});
    pen.line({{3, 16}, {3, 21}, {8, 21}});
    pen.line({{16, 21}, {21, 21}, {21, 16}});
    break;
  case Icon::copy:
    pen.rect(8, 8, 22, 22, 2);
    pen.line({{4, 16}, {3, 16}, {2, 15}, {2, 4}, {4, 2}, {14, 2}, {16, 4}});
    break;
  case Icon::eye:
  case Icon::eye_off:
    list->PathEllipticalArcTo(pen.p(12, 12), ImVec2(10 * pen.s, 6.5f * pen.s), 0, 0, 2 * pi);
    list->PathStroke(pen.col, pen.w, ImDrawFlags_Closed);
    pen.circle(12, 12, 3);
    if (icon == Icon::eye_off) pen.line({{3, 3}, {21, 21}});
    break;
  case Icon::lock:
  case Icon::unlock:
    pen.rect(3, 11, 21, 22, 2);
    pen.to(7, 11), pen.to(7, 7);
    pen.arc_to(12, 7, 5, pi, 2 * pi);
    if (icon == Icon::lock) pen.to(17, 11);
    pen.stroke();
    break;
  case Icon::mail:
    pen.rect(2, 4, 22, 20, 2);
    pen.line({{22, 7}, {13, 12.7f}, {11, 12.7f}, {2, 7}});
    break;
  case Icon::loader: {
    const float spin = float(std::fmod(ImGui::GetTime(), 1.0)) * 2 * pi;
    pen.arc(12, 12, 9, spin, spin + 1.6f * pi);
    request_frames(1);
    break;
  }
  case Icon::dot: pen.disc(12, 12, 5); break;
  // ---- Files and media
  case Icon::folder_open:
    pen.line({{6, 14}, {7.5f, 11}, {21, 11}, {18, 20}, {4, 20}, {2, 18}, {2, 5}, {4, 3}, {8, 3}, {10, 6}, {18, 6},
              {20, 8}, {20, 11}});
    break;
  case Icon::upload:
    pen.line({{21, 15}, {21, 19}, {19, 21}, {5, 21}, {3, 19}, {3, 15}});
    pen.line({{17, 8}, {12, 3}, {7, 8}});
    pen.line({{12, 3}, {12, 15}});
    break;
  case Icon::download:
    pen.line({{21, 15}, {21, 19}, {19, 21}, {5, 21}, {3, 19}, {3, 15}});
    pen.line({{7, 10}, {12, 15}, {17, 10}});
    pen.line({{12, 15}, {12, 3}});
    break;
  case Icon::film:
    pen.rect(3, 3, 21, 21, 2);
    pen.line({{7, 3}, {7, 21}});
    pen.line({{17, 3}, {17, 21}});
    pen.line({{3, 12}, {21, 12}});
    pen.line({{3, 7.5f}, {7, 7.5f}});
    pen.line({{3, 16.5f}, {7, 16.5f}});
    pen.line({{17, 7.5f}, {21, 7.5f}});
    pen.line({{17, 16.5f}, {21, 16.5f}});
    break;
  case Icon::image:
    pen.rect(3, 3, 21, 21, 2);
    pen.circle(9, 9, 2);
    pen.line({{21, 15}, {16, 10}, {5, 21}});
    break;
  case Icon::music:
    pen.line({{9, 18}, {9, 5}, {21, 3}, {21, 16}});
    pen.circle(6, 18, 3);
    pen.circle(18, 16, 3);
    break;
  case Icon::type:
    pen.line({{4, 7}, {4, 4}, {20, 4}, {20, 7}});
    pen.line({{9, 20}, {15, 20}});
    pen.line({{12, 4}, {12, 20}});
    break;
  case Icon::layers:
    pen.poly({{12, 2}, {2, 7}, {12, 12}, {22, 7}}, false);
    pen.line({{2, 17}, {12, 22}, {22, 17}});
    pen.line({{2, 12}, {12, 17}, {22, 12}});
    break;
  // ---- Playback and editing
  case Icon::play: pen.poly({{6, 3}, {20, 12}, {6, 21}}, filled); break;
  case Icon::pause:
    pen.rect(6, 4, 10, 20, 1, filled);
    pen.rect(14, 4, 18, 20, 1, filled);
    break;
  case Icon::skip_back:
    pen.poly({{19, 20}, {9, 12}, {19, 4}}, filled);
    pen.line({{5, 19}, {5, 5}});
    break;
  case Icon::skip_forward:
    pen.poly({{5, 4}, {15, 12}, {5, 20}}, filled);
    pen.line({{19, 5}, {19, 19}});
    break;
  case Icon::volume:
  case Icon::volume_off:
    pen.poly({{11, 5}, {6, 9}, {2, 9}, {2, 15}, {6, 15}, {11, 19}}, false);
    if (icon == Icon::volume) {
      pen.arc(12, 12, 4.5f, -pi / 4, pi / 4);
      pen.arc(12, 12, 9, -pi / 4, pi / 4);
    } else {
      pen.line({{22, 9}, {16, 15}});
      pen.line({{16, 9}, {22, 15}});
    }
    break;
  case Icon::scissors:
    pen.circle(6, 6, 3);
    pen.circle(6, 18, 3);
    pen.line({{20, 4}, {8.12f, 15.88f}});
    pen.line({{14.47f, 14.48f}, {20, 20}});
    pen.line({{8.12f, 8.12f}, {12, 12}});
    break;
  case Icon::trash:
    pen.line({{3, 6}, {21, 6}});
    pen.line({{19, 6}, {19, 20}, {17, 22}, {7, 22}, {5, 20}, {5, 6}});
    pen.line({{8, 6}, {8, 4}, {10, 2}, {14, 2}, {16, 4}, {16, 6}});
    break;
  case Icon::undo:
    pen.line({{9, 14}, {4, 9}, {9, 4}});
    pen.to(4, 9), pen.to(14.5f, 9);
    pen.arc_to(14.5f, 14.5f, 5.5f, -pi / 2, pi / 2);
    pen.to(11, 20);
    pen.stroke();
    break;
  case Icon::redo:
    pen.line({{15, 14}, {20, 9}, {15, 4}});
    pen.to(20, 9), pen.to(9.5f, 9);
    pen.arc_to(9.5f, 14.5f, 5.5f, -pi / 2, -1.5f * pi);
    pen.to(13, 20);
    pen.stroke();
    break;
  case Icon::zoom_in:
  case Icon::zoom_out:
    pen.circle(11, 11, 8);
    pen.line({{21, 21}, {16.65f, 16.65f}});
    pen.line({{8, 11}, {14, 11}});
    if (icon == Icon::zoom_in) pen.line({{11, 8}, {11, 14}});
    break;
  case Icon::magnet:
    pen.to(5, 3), pen.to(5, 12);
    pen.arc_to(12, 12, 7, pi, 0);
    pen.to(19, 3);
    pen.stroke();
    pen.to(9, 3), pen.to(9, 12);
    pen.arc_to(12, 12, 3, pi, 0);
    pen.to(15, 3);
    pen.stroke();
    pen.line({{5, 7}, {9, 7}});
    pen.line({{15, 7}, {19, 7}});
    break;
  case Icon::pointer: pen.poly({{4, 4}, {11, 20}, {13.5f, 13.5f}, {20, 11}}, filled); break;
  }
}

} // namespace silta
