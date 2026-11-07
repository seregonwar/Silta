#pragma once
// Transitions for immediate-mode UI: each animated value lives under an ID and
// eases toward the target it is given every frame, like a CSS transition.
//
//   const float hover = silta::anim::tween(id_hover, hovered ? 1.f : 0.f);
#include "imgui.h"

namespace silta::anim {

// CSS-like timing functions.
enum class Ease {
  linear,
  standard,    // Tailwind's default transition: cubic-bezier(0.4, 0, 0.2, 1)
  ease_out,    // cubic-bezier(0, 0, 0.2, 1)
  ease_in_out, // cubic-bezier(0.4, 0, 0.6, 1)
};

// Tailwind's `transition` duration.
inline constexpr float default_duration = 0.15f;

// Value eased toward `target`. The first call returns `target` (no animation
// on first appearance); later target changes start a new transition from the
// current value.
float tween(ImGuiID id, float target, float duration = default_duration, Ease ease = Ease::standard);

// Jumps a value without animating (e.g. restart an enter animation from 0).
void reset(ImGuiID id, float value);

// ID for one animated property of an item: sub_id(item_id, "hover").
ImGuiID sub_id(ImGuiID item, const char *property);

// Evaluates a CSS cubic-bezier(x1, y1, x2, y2) at t in [0, 1].
float cubic_bezier(float x1, float y1, float x2, float y2, float t);

// Internal: called by silta::begin_frame / end_frame.
void begin_frame();
void end_frame();
bool running();

} // namespace silta::anim
