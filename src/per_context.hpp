#pragma once
// State that belongs to one Dear ImGui context (internal).
//
// Silta keeps layout memory, animations, overlay state, toasts and retired
// textures per ImGui context, so several contexts (windows, a projector, an
// embedded editor) can use Silta side by side. Each module declares its state
// struct and reaches it with per_context<T>(). The state is created on first
// use and freed when the ImGui context is destroyed.
#include <memory>
#include "imgui.h"

namespace silta::detail {

struct Slots;
Slots &slots(ImGuiContext *context);
void *slot(Slots &s, int index);
void set_slot(Slots &s, int index, std::shared_ptr<void> value);
int new_slot_index();
// True while `context` has Silta state (it has not been destroyed).
bool context_alive(ImGuiContext *context);
// Frees the Silta state of `context` (done automatically on DestroyContext).
void release_context(ImGuiContext *context);

// Per-frame hooks of modules that keep per-context state.
void overlay_begin_frame();

template <class T> T &per_context(ImGuiContext *context = nullptr) {
  static const int index = new_slot_index();
  Slots &s = slots(context ? context : ImGui::GetCurrentContext());
  if (void *p = slot(s, index)) return *static_cast<T *>(p);
  auto value = std::make_shared<T>();
  T &ref = *value;
  set_slot(s, index, std::move(value));
  return ref;
}

} // namespace silta::detail
