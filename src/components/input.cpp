#include "silta/components/input.hpp"
#include "common.hpp"
#include <cfloat>
#include <string>

namespace silta {
namespace {
// What Input and Textarea share.
struct Field {
  const char *placeholder = nullptr;
  float width = fill;
  float height = 36;  // points
  float pad_y = 0;    // points; 0 centres one line vertically
  bool disabled = false;
};

// Grows a std::string as the user types (ImGui's resize callback).
int resize_string(ImGuiInputTextCallbackData *data) {
  if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
    auto *str = static_cast<std::string *>(data->UserData);
    str->resize(size_t(data->BufTextLen));
    data->Buf = str->data();
  }
  return 0;
}

// Input:    h-9 w-full rounded-md border border-input bg-transparent px-3 py-1
//           text-sm shadow-xs, focus-visible: border-ring ring-ring/50 ring-[3px]
// Textarea: the same with min-h-16 py-2 and field-sizing: content.
template <class Edit> bool field(const char *id, const Field &f, Edit edit) {
  const Theme &t = theme();
  const float h = px(f.height);
  const float w = layout::resolve_width(f.width, px(240));
  const ImRect box = layout::allocate(ImVec2(w, h), f.width == fill);
  ImDrawList *dl = ImGui::GetWindowDrawList();
  const float radius = px(t.radius_md());

  detail::DisabledScope disabled(f.disabled);
  draw::shadow(dl, box, radius, draw::shadow_xs());
  draw::fill(dl, box, t.is_dark() ? t.input.fade(0.3f) : transparent, radius);

  // ImGui's InputText does the editing (caret, selection, clipboard, IME),
  // with its frame made invisible: Silta draws the box around it.
  // Font size in pixels; ImGui multiplies the pushed size by the host's own
  // global scales, so divide them out (the host may use another scale).
  const float fs = imgui_font_size(font_points(TextSize::sm));
  const ImGuiStyle &style = ImGui::GetStyle();
  const float host_scale = std::max(1e-3f, style.FontScaleMain * style.FontScaleDpi);
  const float pad_y = f.pad_y > 0 ? px(f.pad_y) : std::floor((h - fs) / 2);
  ImGui::PushFont(font_for(Weight::regular), fs / host_scale);
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(px(12), pad_y));
  ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.f);
  ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, px(8));
  ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarRounding, px(4));
  // Silta's colours, whatever the host's ImGui style is.
  ImGui::PushStyleColor(ImGuiCol_Text, t.foreground.vec4());
  ImGui::PushStyleColor(ImGuiCol_TextDisabled, t.muted_foreground.vec4());
  ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_TextSelectedBg, t.ring.fade(0.35f).vec4());
  ImGui::PushStyleColor(ImGuiCol_InputTextCursor, t.foreground.vec4());
  ImGui::PushStyleColor(ImGuiCol_NavCursor, ImVec4(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, ImVec4(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, t.border.vec4());
  ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered, mix(t.border, t.muted_foreground, 0.4f).vec4());
  ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabActive, t.muted_foreground.vec4());
  ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));

  const ImVec2 saved = ImGui::GetCursorScreenPos();
  ImGui::SetCursorScreenPos(box.Min);
  ImGui::SetNextItemWidth(box.GetWidth());
  const std::string label = std::string("##") + id;
  // Escape leaves the field and keeps the text, as browsers do (ImGui's
  // InputText would revert the edit).
  if (ImGui::GetActiveID() == ImGui::GetID(label.c_str()) && ImGui::IsKeyPressed(ImGuiKey_Escape, false))
    ImGui::ClearActiveID();
  const bool changed = edit(label.c_str(), box.GetSize());
  const ImGuiID item = ImGui::GetItemID();
  const bool active = ImGui::IsItemActive();
  ImGui::SetCursorScreenPos(saved);

  ImGui::PopStyleColor(13);
  ImGui::PopStyleVar(4);
  ImGui::PopFont();

  const ImGuiContext &g = *GImGui;
  const bool focused = !f.disabled && (active || (g.NavId == item && g.NavCursorVisible));
  const float focus_t = anim::tween(anim::sub_id(item, "focus"), focused ? 1.f : 0.f);
  draw::border(dl, box, mix(t.input, t.ring, focus_t), radius, px(1));
  if (focus_t > 0) draw::focus_ring(dl, box, radius, t.ring.fade(focus_t));
  return changed;
}
} // namespace

bool Input(const char *id, char *buffer, std::size_t buffer_size, const InputOptions &o) {
  ImGuiInputTextFlags flags = o.flags | (o.password ? ImGuiInputTextFlags_Password : 0);
  return field(id, {.placeholder = o.placeholder, .width = o.width, .disabled = o.disabled},
               [&](const char *label, ImVec2) {
                 return ImGui::InputTextWithHint(label, o.placeholder ? o.placeholder : "", buffer, buffer_size, flags);
               });
}

bool Input(const char *id, std::string *text, const InputOptions &o) {
  ImGuiInputTextFlags flags = o.flags | (o.password ? ImGuiInputTextFlags_Password : 0);
  return field(id, {.placeholder = o.placeholder, .width = o.width, .disabled = o.disabled},
               [&](const char *label, ImVec2) {
                 return ImGui::InputTextWithHint(label, o.placeholder ? o.placeholder : "", text->data(),
                                                 text->capacity() + 1, flags | ImGuiInputTextFlags_CallbackResize,
                                                 resize_string, text);
               });
}

bool Textarea(const char *id, std::string *text, const TextareaOptions &o) {
  // field-sizing: content: the box grows with its wrapped text, from
  // min_height to max_height, then scrolls. Measured with the current width.
  const ImGuiID height_id = anim::sub_id(ImGui::GetID(id), "textarea.height");
  ImGuiStorage *storage = ImGui::GetStateStorage();
  const float width_px = storage->GetFloat(anim::sub_id(height_id, "width"), 0.f);
  const float fs = imgui_font_size(font_points(TextSize::sm));
  const float pad_y = 8, pad_x = px(12);
  float content = 0;
  if (width_px > 0) {
    ImFont *font = font_for(Weight::regular);
    const float wrap = o.wrap ? std::max(1.f, width_px - 2 * pad_x - px(8)) : 0.f;
    const char *s = text->c_str();
    content = font->CalcTextSizeA(fs, FLT_MAX, wrap, s, s + text->size()).y;
    if (!text->empty() && text->back() == '\n') content += fs; // the caret's empty last line
  }
  const float wanted = std::clamp(content / scale() + 2 * pad_y + 2, o.min_height,
                                  std::max(o.min_height, o.max_height));
  ImGuiInputTextFlags flags = o.flags | ImGuiInputTextFlags_CallbackResize | (o.wrap ? ImGuiInputTextFlags_WordWrap : 0);
  bool show_placeholder = false;
  ImRect placed;
  const bool changed = field(id,
                             {.placeholder = o.placeholder, .width = o.width, .height = wanted, .pad_y = pad_y,
                              .disabled = o.disabled},
                             [&](const char *label, ImVec2 size) {
                               storage->SetFloat(anim::sub_id(height_id, "width"), size.x);
                               const bool r = ImGui::InputTextMultiline(label, text->data(), text->capacity() + 1,
                                                                        size, flags, resize_string, text);
                               placed = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
                               show_placeholder = text->empty() && !ImGui::IsItemActive();
                               return r;
                             });
  // InputTextMultiline has no hint: draw the placeholder ourselves, on the
  // first line (ImGui's line height is the font size).
  if (show_placeholder && o.placeholder)
    draw_text(ImGui::GetWindowDrawList(), ImVec2(placed.Min.x + pad_x, placed.Min.y + px(pad_y)), o.placeholder,
              TextSize::sm, Weight::regular, theme().muted_foreground,
              o.wrap ? placed.GetWidth() - 2 * pad_x : 0.f, fs / scale());
  return changed;
}

} // namespace silta
