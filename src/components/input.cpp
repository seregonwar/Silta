#include "silta/components/input.hpp"
#include "common.hpp"
#include "misc/cpp/imgui_stdlib.h"
#include <string>

namespace silta {
namespace {
// h-9 w-full rounded-md border border-input bg-transparent px-3 py-1 text-sm
// shadow-xs, focus-visible: border-ring ring-ring/50 ring-[3px].
template <class Edit> bool field(const char *id, const InputOptions &o, Edit edit) {
  const Theme &t = theme();
  const float h = px(36);
  const float w = layout::resolve_width(o.width, px(240));
  const ImRect box = layout::allocate(ImVec2(w, h), o.width == fill);
  ImDrawList *dl = ImGui::GetWindowDrawList();
  const float radius = px(t.radius_md());

  detail::DisabledScope disabled(o.disabled);
  draw::shadow(dl, box, radius, draw::shadow_xs());
  draw::fill(dl, box, t.is_dark() ? t.input.fade(0.3f) : transparent, radius);

  // ImGui's InputText does the editing (caret, selection, clipboard, IME),
  // with its frame made invisible: Silta draws the box around it.
  const float fs = imgui_font_size(font_points(TextSize::sm));
  ImGui::PushFont(font_for(Weight::regular), font_points(TextSize::sm) * fonts().em_to_imgui);
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(px(12), std::floor((h - fs) / 2)));
  ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.f);
  ImGui::PushStyleColor(ImGuiCol_Text, t.foreground.vec4());
  ImGui::PushStyleColor(ImGuiCol_TextDisabled, t.muted_foreground.vec4());

  const ImVec2 saved = ImGui::GetCursorScreenPos();
  ImGui::SetCursorScreenPos(box.Min);
  ImGui::SetNextItemWidth(box.GetWidth());
  const std::string label = std::string("##") + id;
  ImGuiInputTextFlags flags = o.flags;
  if (o.password) flags |= ImGuiInputTextFlags_Password;
  // Escape leaves the field and keeps the text, as browsers do (ImGui's
  // InputText would revert the edit).
  if (ImGui::GetActiveID() == ImGui::GetID(label.c_str()) && ImGui::IsKeyPressed(ImGuiKey_Escape, false))
    ImGui::ClearActiveID();
  const bool changed = edit(label.c_str(), flags);
  const ImGuiID item = ImGui::GetItemID();
  const bool active = ImGui::IsItemActive();
  ImGui::SetCursorScreenPos(saved);

  ImGui::PopStyleColor(2);
  ImGui::PopStyleVar(2);
  ImGui::PopFont();

  const ImGuiContext &g = *GImGui;
  const bool focused = !o.disabled && (active || (g.NavId == item && g.NavCursorVisible));
  const float focus_t = anim::tween(anim::sub_id(item, "focus"), focused ? 1.f : 0.f);
  draw::border(dl, box, mix(t.input, t.ring, focus_t), radius, px(1));
  if (focus_t > 0) draw::focus_ring(dl, box, radius, t.ring.fade(focus_t));
  return changed;
}
} // namespace

bool Input(const char *id, char *buffer, std::size_t buffer_size, const InputOptions &o) {
  return field(id, o, [&](const char *label, ImGuiInputTextFlags flags) {
    return ImGui::InputTextWithHint(label, o.placeholder ? o.placeholder : "", buffer, buffer_size, flags);
  });
}

bool Input(const char *id, std::string *text, const InputOptions &o) {
  return field(id, o, [&](const char *label, ImGuiInputTextFlags flags) {
    return ImGui::InputTextWithHint(label, o.placeholder ? o.placeholder : "", text, flags);
  });
}

} // namespace silta
