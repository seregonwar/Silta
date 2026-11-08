#include "silta/anim.hpp"
#include "imgui_internal.h"
#include "per_context.hpp"
#include <cmath>
#include <unordered_map>

namespace silta::anim {
namespace {
struct Tween {
  float from = 0, to = 0, value = 0;
  double start = 0;
  float duration = 0;
  Ease ease = Ease::standard;
  int last_frame = 0;
};

struct AnimState {
  std::unordered_map<ImGuiID, Tween> tweens;
  bool any_running = false;
};
AnimState &S() { return detail::per_context<AnimState>(); }

float apply(Ease ease, float t) {
  switch (ease) {
  case Ease::linear: return t;
  case Ease::standard: return cubic_bezier(0.4f, 0.f, 0.2f, 1.f, t);
  case Ease::ease_out: return cubic_bezier(0.f, 0.f, 0.2f, 1.f, t);
  case Ease::ease_in_out: return cubic_bezier(0.4f, 0.f, 0.6f, 1.f, t);
  }
  return t;
}
} // namespace

float cubic_bezier(float x1, float y1, float x2, float y2, float t) {
  if (t <= 0) return 0;
  if (t >= 1) return 1;
  // Solve x(s) = t for s with Newton, falling back to bisection, then y(s).
  auto bez = [](float a, float b, float s) {
    const float u = 1 - s;
    return 3 * u * u * s * a + 3 * u * s * s * b + s * s * s;
  };
  auto dbez = [](float a, float b, float s) {
    const float u = 1 - s;
    return 3 * u * u * a + 6 * u * s * (b - a) + 3 * s * s * (1 - b);
  };
  float s = t;
  for (int i = 0; i < 8; ++i) {
    const float x = bez(x1, x2, s) - t;
    if (std::fabs(x) < 1e-5f) return bez(y1, y2, s);
    const float d = dbez(x1, x2, s);
    if (std::fabs(d) < 1e-6f) break;
    s -= x / d;
  }
  float lo = 0, hi = 1;
  s = t;
  for (int i = 0; i < 30; ++i) {
    const float x = bez(x1, x2, s);
    if (std::fabs(x - t) < 1e-5f) break;
    (x < t ? lo : hi) = s;
    s = (lo + hi) / 2;
  }
  return bez(y1, y2, s);
}

ImGuiID sub_id(ImGuiID item, const char *property) { return ImHashStr(property, 0, item); }

float tween(ImGuiID id, float target, float duration, Ease ease) {
  const double now = ImGui::GetTime();
  const int frame = ImGui::GetFrameCount();
  auto [it, inserted] = S().tweens.try_emplace(id);
  Tween &tw = it->second;
  tw.last_frame = frame;
  if (inserted) {
    tw.from = tw.to = tw.value = target;
    return target;
  }
  if (target != tw.to) {
    tw.from = tw.value;
    tw.to = target;
    tw.start = now;
    tw.duration = duration;
    tw.ease = ease;
  }
  if (tw.value == tw.to) return tw.value;
  const float t = tw.duration > 0 ? float((now - tw.start) / tw.duration) : 1.f;
  if (t >= 1) {
    tw.value = tw.to;
  } else {
    tw.value = tw.from + (tw.to - tw.from) * apply(tw.ease, t);
    S().any_running = true;
  }
  return tw.value;
}

void reset(ImGuiID id, float value) {
  Tween &tw = S().tweens[id];
  tw.from = tw.to = tw.value = value;
  tw.last_frame = ImGui::GetFrameCount();
}

void begin_frame() { S().any_running = false; }

void end_frame() {
  // Forget values whose item has not been drawn for a while.
  const int frame = ImGui::GetFrameCount();
  if (frame % 60 != 0) return;
  for (auto it = S().tweens.begin(); it != S().tweens.end();)
    it = frame - it->second.last_frame > 120 ? S().tweens.erase(it) : std::next(it);
}

bool running() { return S().any_running; }

} // namespace silta::anim
