#include "silta/components/controls.hpp"
#include "silta/components/overlay.hpp"
#include "common.hpp"
#include <string>
#include <vector>

namespace silta {

// =============================================================== Checkbox
bool Checkbox(const char *label, bool *value, const CheckboxOptions &o) {
  const Theme &t = theme();
  const ImGuiID id = ImGui::GetID(label);
  const std::string text(label, detail::label_end(label));
  const float size = px(16), gap = px(8);
  const float line = px(font_points(TextSize::sm));
  const ImVec2 ts = text.empty() ? ImVec2(0, 0)
                                 : measure_text(text.c_str(), TextSize::sm, Weight::medium, 0, font_points(TextSize::sm));
  const ImRect box = layout::allocate(ImVec2(size + (text.empty() ? 0 : gap + ts.x), std::max(size, line)));
  const float top = std::floor(box.GetCenter().y - size / 2);
  const bool mirrored = is_rtl();
  const float square_x = mirrored ? box.Max.x - size : box.Min.x;
  const ImRect square(ImVec2(square_x, top), ImVec2(square_x + size, top + size));

  detail::DisabledScope disabled(o.disabled);
  const detail::Interaction in = detail::interact(id, box, o.disabled);
  if (in.pressed) {
    *value = !*value;
    ImGui::MarkItemEdited(id);
  }
  const float on = anim::tween(anim::sub_id(id, "on"), *value ? 1.f : 0.f, 0.1f);
  ImDrawList *dl = ImGui::GetWindowDrawList();
  const float radius = px(4);
  draw::shadow(dl, square, radius, draw::shadow_xs());
  draw::fill(dl, square, mix(t.is_dark() ? t.input.fade(0.3f) : transparent, t.primary, on), radius);
  Color border = mix(t.input, t.primary, on);
  if (in.focus_t > 0) {
    border = mix(border, t.ring, in.focus_t);
    draw::focus_ring(dl, square, radius, t.ring.fade(in.focus_t));
  }
  draw::border(dl, square, border, radius, px(1));
  if (on > 0) {
    const float icon = px(14);
    draw_icon(dl, Icon::check, ImVec2(square.GetCenter().x - icon / 2, square.GetCenter().y - icon / 2), icon,
              t.primary_foreground.fade(on));
  }
  if (!text.empty())
    draw_text(dl, ImVec2(mirrored ? box.Min.x : square.Max.x + gap, std::floor(box.GetCenter().y - ts.y / 2)),
              text.c_str(), TextSize::sm, Weight::medium, t.foreground, 0, font_points(TextSize::sm));
  return in.pressed;
}

// ================================================================= Slider
bool Slider(const char *sid, float *value, float min, float max, const SliderOptions &o) {
  const Theme &t = theme();
  const ImGuiID id = ImGui::GetID(sid);
  const float thumb = px(16), track_h = px(6);
  const float w = layout::resolve_width(o.width, px(200));
  const ImRect box = layout::allocate(ImVec2(w, thumb), o.width == fill);
  // The thumb travels inside the box so it never overhangs.
  const float x0 = box.Min.x + thumb / 2, x1 = box.Max.x - thumb / 2;
  const bool mirrored = is_rtl();

  detail::DisabledScope disabled(o.disabled);
  detail::Interaction in = detail::interact(id, box, o.disabled);
  const float before = *value;
  auto snap = [&](float v) {
    v = std::clamp(v, min, max);
    if (o.step > 0) v = min + std::round((v - min) / o.step) * o.step;
    return std::clamp(v, min, max);
  };
  if (in.held && max > min) {
    const float mx = ImGui::GetIO().MousePos.x;
    const float f = std::clamp((mirrored ? x1 - mx : mx - x0) / std::max(1.f, x1 - x0), 0.f, 1.f);
    *value = snap(min + f * (max - min));
  }
  const ImGuiContext &g = *GImGui;
  if (!o.disabled && g.NavId == id && g.ActiveId == 0) {
    const float step = o.step > 0 ? o.step : (max - min) / 100.f;
    if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) *value = snap(*value + (mirrored ? step : -step));
    if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) *value = snap(*value + (mirrored ? -step : step));
    if (ImGui::IsKeyPressed(ImGuiKey_Home)) *value = min;
    if (ImGui::IsKeyPressed(ImGuiKey_End)) *value = max;
    // Left/right adjust the value, so navigation must not also move focus
    // (up/down still move between controls).
    ImGui::SetKeyOwner(ImGuiKey_LeftArrow, id);
    ImGui::SetKeyOwner(ImGuiKey_RightArrow, id);
  }
  const bool changed = *value != before;
  if (changed) ImGui::MarkItemEdited(id);

  const float f = max > min ? (*value - min) / (max - min) : 0.f;
  const float cx = mirrored ? x1 - (x1 - x0) * f : x0 + (x1 - x0) * f, cy = box.GetCenter().y;
  ImDrawList *dl = ImGui::GetWindowDrawList();
  const ImRect track(ImVec2(box.Min.x, cy - track_h / 2), ImVec2(box.Max.x, cy + track_h / 2));
  draw::fill(dl, track, t.muted, track_h / 2);
  if (mirrored) draw::fill(dl, ImRect(ImVec2(cx, track.Min.y), track.Max), t.primary, track_h / 2);
  else draw::fill(dl, ImRect(track.Min, ImVec2(cx, track.Max.y)), t.primary, track_h / 2);
  // hover:ring-4 focus-visible:ring-4 ring-ring/50
  const float ring = std::max(in.hover_t, in.focus_t);
  if (ring > 0) dl->AddCircleFilled(ImVec2(cx, cy), thumb / 2 + px(4), t.ring.fade(0.5f * ring).u32(), 32);
  draw::shadow(dl, ImRect(ImVec2(cx - thumb / 2, cy - thumb / 2), ImVec2(cx + thumb / 2, cy + thumb / 2)), thumb / 2,
               draw::shadow_sm());
  dl->AddCircleFilled(ImVec2(cx, cy), thumb / 2, t.background.u32(), 32);
  dl->AddCircle(ImVec2(cx, cy), thumb / 2 - px(0.5f), t.primary.u32(), 32, px(1));
  return changed;
}

// =================================================================== Tabs
bool Tabs(const char *sid, int *active, std::span<const char *const> labels, const TabsOptions &o) {
  const Theme &t = theme();
  ImGui::PushID(sid);
  const ImGuiID list_id = ImGui::GetID("##list");
  const float pad = px(3), h = px(36);
  std::vector<float> natural(labels.size());
  float total = 0;
  for (size_t i = 0; i < labels.size(); ++i) {
    natural[i] = measure_text(labels[i], TextSize::sm, Weight::medium).x + px(16) + px(2);
    total += natural[i];
  }
  const float w = layout::resolve_width(o.width, total + 2 * pad);
  const ImRect box = layout::allocate(ImVec2(w, h), o.width == fill);
  ImDrawList *dl = ImGui::GetWindowDrawList();
  draw::fill(dl, box, t.muted, px(t.radius_lg()));

  // Equal tabs when the list is wider than its labels (flex-1), else natural.
  const float inner = box.GetWidth() - 2 * pad;
  const bool equal = inner > total + 0.5f;
  std::vector<ImRect> rects(labels.size());
  const bool mirrored = is_rtl();
  float x = mirrored ? box.Max.x - pad : box.Min.x + pad;
  for (size_t i = 0; i < labels.size(); ++i) {
    const float tw = equal ? inner / float(labels.size()) : natural[i];
    rects[i] = mirrored ? ImRect(ImVec2(std::floor(x - tw), box.Min.y + pad), ImVec2(std::floor(x), box.Max.y - pad))
                        : ImRect(ImVec2(std::floor(x), box.Min.y + pad), ImVec2(std::floor(x + tw), box.Max.y - pad));
    x += mirrored ? -tw : tw;
  }
  // Sliding active background.
  bool changed = false;
  if (*active >= 0 && *active < int(labels.size())) {
    // Animated in tab-index space, so only a change of tab moves it (not a
    // change of layout).
    const float at = anim::tween(anim::sub_id(list_id, "index"), float(*active), 0.2f);
    const int i0 = std::clamp(int(std::floor(at)), 0, int(labels.size()) - 1);
    const int i1 = std::min(i0 + 1, int(labels.size()) - 1);
    const float f = at - float(i0);
    const ImRect &a = rects[size_t(i0)], &b = rects[size_t(i1)];
    const ImRect ind(ImVec2(a.Min.x + (b.Min.x - a.Min.x) * f, a.Min.y), ImVec2(a.Max.x + (b.Max.x - a.Max.x) * f, a.Max.y));
    const float radius = px(t.radius_md());
    if (t.is_dark()) {
      draw::fill(dl, ind, t.input.fade(0.3f), radius);
      draw::border(dl, ind, t.input, radius, px(1));
    } else {
      draw::shadow(dl, ind, radius, draw::shadow_sm());
      draw::fill(dl, ind, t.background, radius);
    }
  }
  for (size_t i = 0; i < labels.size(); ++i) {
    ImGui::PushID(int(i));
    const detail::Interaction in = detail::interact(ImGui::GetID("##tab"), rects[i], false);
    if (in.pressed && *active != int(i)) *active = int(i), changed = true;
    if (in.focus_t > 0) draw::focus_ring(dl, rects[i], px(t.radius_md()), t.ring.fade(in.focus_t));
    const bool on = *active == int(i);
    const Color ink = on || !t.is_dark() ? t.foreground : mix(t.muted_foreground, t.foreground, in.hover_t);
    const ImVec2 ts = measure_text(labels[i], TextSize::sm, Weight::medium);
    draw_text(dl, ImVec2(std::floor(rects[i].GetCenter().x - ts.x / 2), std::floor(rects[i].GetCenter().y - ts.y / 2)),
              labels[i], TextSize::sm, Weight::medium, ink);
    ImGui::PopID();
  }
  ImGui::PopID();
  return changed;
}

// ================================================================= Toggle
namespace {
struct ToggleLook {
  float height, pad;
};
ToggleLook toggle_size(Size s) {
  switch (s) {
  case Size::sm: return {32, 6};
  case Size::lg: return {40, 10};
  default: return {36, 8};
  }
}

// One toggle face. `corners` picks the rounded corners (for joined groups).
bool toggle_face(ImGuiID id, const char *text, Icon icon, bool on, const ToggleOptions &o, ImDrawFlags corners,
                 bool draw_border, float min_w = 0) {
  const Theme &t = theme();
  const ToggleLook look = toggle_size(o.size);
  const float h = px(look.height), icon_px = px(16), gap = px(8);
  const ImVec2 ts = text && *text ? measure_text(text, TextSize::sm, Weight::medium) : ImVec2(0, 0);
  float content = ts.x;
  if (icon != Icon::none) content += icon_px + (ts.x > 0 ? gap : 0);
  const float w = std::max({h, content + 2 * px(look.pad), min_w});
  const ImRect box = layout::allocate(ImVec2(w, h));
  detail::DisabledScope disabled(o.disabled);
  const detail::Interaction in = detail::interact(id, box, o.disabled);
  const float on_t = anim::tween(anim::sub_id(id, "on"), on ? 1.f : 0.f);
  ImDrawList *dl = ImGui::GetWindowDrawList();
  const float radius = px(t.radius_md());
  if (o.outline && draw_border) draw::shadow(dl, box, radius, draw::shadow_xs());
  const Color hover_bg = o.outline ? t.accent : t.muted;
  Color bg = mix(transparent, hover_bg, in.hover_t * (1 - on_t));
  bg = mix(bg, t.accent, on_t);
  if (bg.a > 0) dl->AddRectFilled(box.Min, box.Max, bg.u32(), radius, corners);
  if (o.outline && draw_border) {
    const float hw = px(1) / 2;
    dl->AddRect(ImVec2(box.Min.x + hw, box.Min.y + hw), ImVec2(box.Max.x - hw, box.Max.y - hw), t.input.u32(),
                radius, px(1), corners);
  }
  if (in.focus_t > 0) draw::focus_ring(dl, box, radius, t.ring.fade(in.focus_t));
  Color ink = mix(t.foreground, t.muted_foreground, in.hover_t * (1 - on_t) * (o.outline ? 0 : 1));
  ink = mix(ink, t.accent_foreground, on_t);
  float x = std::floor(box.GetCenter().x - content / 2);
  const float cy = box.GetCenter().y;
  if (is_rtl()) {
    if (ts.x > 0) {
      draw_text(dl, ImVec2(x, std::floor(cy - ts.y / 2)), text, TextSize::sm, Weight::medium, ink);
      x += ts.x;
    }
    if (icon != Icon::none) {
      if (ts.x > 0) x += gap;
      draw_icon(dl, icon, ImVec2(x, std::floor(cy - icon_px / 2)), icon_px, ink);
    }
  } else {
    if (icon != Icon::none) {
      draw_icon(dl, icon, ImVec2(x, std::floor(cy - icon_px / 2)), icon_px, ink);
      x += icon_px + gap;
    }
    if (ts.x > 0) draw_text(dl, ImVec2(x, std::floor(cy - ts.y / 2)), text, TextSize::sm, Weight::medium, ink);
  }
  return in.pressed;
}
} // namespace

bool Toggle(const char *label, bool *on, const ToggleOptions &o) {
  const ImGuiID id = ImGui::GetID(label);
  const std::string text(label, detail::label_end(label));
  if (!toggle_face(id, text.c_str(), o.icon, *on, o, ImDrawFlags_RoundCornersAll, true)) return false;
  *on = !*on;
  return true;
}

bool ToggleGroup(const char *sid, int *value, std::span<const ToggleGroupItem> items, const ToggleOptions &o) {
  ImGui::PushID(sid);
  bool changed = false;
  {
    auto row = Row({.gap = o.outline ? 0.f : 4.f, .align = Align::center});
    for (size_t i = 0; i < items.size(); ++i) {
      ImGui::PushID(int(i));
      ImDrawFlags corners = ImDrawFlags_RoundCornersNone;
      if (!o.outline || items.size() == 1) corners = ImDrawFlags_RoundCornersAll;
      else if (i == 0) corners = ImDrawFlags_RoundCornersLeft;
      else if (i + 1 == items.size()) corners = ImDrawFlags_RoundCornersRight;
      const bool pressed = toggle_face(ImGui::GetID("##item"), items[i].label, items[i].icon, *value == int(i), o,
                                       corners, true);
      if (items[i].tooltip) Tooltip(items[i].tooltip);
      if (pressed && *value != int(i)) *value = int(i), changed = true;
      ImGui::PopID();
    }
  }
  ImGui::PopID();
  return changed;
}

// =============================================================== Progress
void Progress(const char *sid, float value, const ProgressOptions &o) {
  const Theme &t = theme();
  const ImGuiID id = ImGui::GetID(sid);
  const float h = px(8);
  const ImRect box = layout::allocate(ImVec2(layout::resolve_width(o.width, px(240)), h), o.width == fill);
  const float shown = anim::tween(anim::sub_id(id, "value"), std::clamp(value, 0.f, 1.f), 0.3f);
  ImDrawList *dl = ImGui::GetWindowDrawList();
  draw::fill(dl, box, t.primary.fade(0.2f), h / 2);
  if (shown > 0) {
    const float filled = std::max(h, box.GetWidth() * shown);
    const ImRect bar = is_rtl() ? ImRect(ImVec2(box.Max.x - filled, box.Min.y), box.Max)
                                : ImRect(box.Min, ImVec2(box.Min.x + filled, box.Max.y));
    draw::fill(dl, bar, t.primary, h / 2);
  }
}

// ==================================================================== Kbd
void Kbd(const char *keys) {
  const Theme &t = theme();
  const ImVec2 ts = measure_text(keys, TextSize::xs, Weight::medium);
  const float h = px(20);
  const ImRect box = layout::allocate(ImVec2(std::max(h, ts.x + px(8)), h));
  ImDrawList *dl = ImGui::GetWindowDrawList();
  draw::fill(dl, box, t.muted, px(t.radius_sm()));
  draw_text(dl, ImVec2(std::floor(box.GetCenter().x - ts.x / 2), std::floor(box.GetCenter().y - ts.y / 2)), keys,
            TextSize::xs, Weight::medium, t.muted_foreground);
}

} // namespace silta
