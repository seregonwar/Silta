#pragma once
// Shared behaviour of interactive components (internal).
#include "silta/anim.hpp"
#include "silta/context.hpp"
#include "silta/draw.hpp"
#include "silta/layout.hpp"
#include "silta/text.hpp"
#include "imgui.h"
#include "imgui_internal.h"
#include <algorithm>
#include <cmath>

namespace silta::detail {

struct Interaction {
  bool hovered = false;
  bool held = false;
  bool pressed = false;
  // Keyboard/gamepad focus that should show a ring (CSS :focus-visible).
  bool focus_visible = false;
  // Animated 0..1 versions.
  float hover_t = 0, focus_t = 0;
};

// Registers the item with ImGui (hit testing, keyboard/gamepad navigation)
// and returns its state.
inline Interaction interact(ImGuiID id, const ImRect &box, bool disabled) {
  Interaction in;
  if (!ImGui::ItemAdd(box, id)) return in;
  if (!disabled) in.pressed = ImGui::ButtonBehavior(box, id, &in.hovered, &in.held);
  const ImGuiContext &g = *GImGui;
  in.focus_visible = !disabled && g.NavId == id && g.NavCursorVisible;
  in.hover_t = anim::tween(anim::sub_id(id, "hover"), in.hovered ? 1.f : 0.f);
  in.focus_t = anim::tween(anim::sub_id(id, "focus"), in.focus_visible ? 1.f : 0.f);
  return in;
}

// Label text up to "##" (ImGui's convention for hidden ID suffixes).
inline const char *label_end(const char *label) { return ImGui::FindRenderedTextEnd(label); }

// RAII: a disabled scope fades the component (opacity-50) and blocks input.
struct DisabledScope {
  bool on;
  explicit DisabledScope(bool disabled) : on(disabled) {
    if (on) ImGui::BeginDisabled(true);
  }
  ~DisabledScope() {
    if (on) ImGui::EndDisabled();
  }
};

} // namespace silta::detail
