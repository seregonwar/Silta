#include "per_context.hpp"
#include "imgui_internal.h"
#include <unordered_map>
#include <vector>

namespace silta::detail {

struct Slots {
  std::vector<std::shared_ptr<void>> items;
};

namespace {
std::unordered_map<ImGuiContext *, std::unique_ptr<Slots>> &all() {
  static std::unordered_map<ImGuiContext *, std::unique_ptr<Slots>> map;
  return map;
}
// One-entry cache: the UI thread asks for the same context many times a frame.
ImGuiContext *cached_context = nullptr;
Slots *cached_slots = nullptr;
int slot_count = 0;

void on_shutdown(ImGuiContext *context, ImGuiContextHook *) { release_context(context); }
} // namespace

int new_slot_index() { return slot_count++; }

Slots &slots(ImGuiContext *context) {
  IM_ASSERT(context && "silta: no current ImGui context");
  if (context == cached_context) return *cached_slots;
  auto &entry = all()[context];
  if (!entry) {
    entry = std::make_unique<Slots>();
    // Free this context's state when ImGui destroys the context.
    ImGuiContextHook hook;
    hook.Type = ImGuiContextHookType_Shutdown;
    hook.Callback = on_shutdown;
    ImGui::AddContextHook(context, &hook);
  }
  cached_context = context;
  cached_slots = entry.get();
  return *entry;
}

void *slot(Slots &s, int index) { return index < int(s.items.size()) ? s.items[size_t(index)].get() : nullptr; }

void set_slot(Slots &s, int index, std::shared_ptr<void> value) {
  if (index >= int(s.items.size())) s.items.resize(size_t(index) + 1);
  s.items[size_t(index)] = std::move(value);
}

bool context_alive(ImGuiContext *context) { return context && all().count(context) > 0; }

void release_context(ImGuiContext *context) {
  auto it = all().find(context);
  if (it == all().end()) return;
  // Detach first: destructors of the state may look the context up again.
  std::unique_ptr<Slots> owned = std::move(it->second);
  all().erase(it);
  if (cached_context == context) cached_context = nullptr, cached_slots = nullptr;
  owned.reset();
}

} // namespace silta::detail
