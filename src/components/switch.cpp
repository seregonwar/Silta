#include "silta/components/switch.hpp"
#include "common.hpp"
#include <string>

namespace silta {

bool Switch(const char *label, bool *value, const SwitchOptions &o) {
  const ImGuiID id = ImGui::GetID(label);
  const std::string text(label, detail::label_end(label));
  // Track h-[1.15rem] w-8, thumb size-4, label gap-2 (text-sm font-medium leading-none).
  const float track_w = px(32), track_h = std::round(px(18.4f)), thumb = px(16), gap = px(8);
  const float line = px(font_points(TextSize::sm));
  const ImVec2 ts = text.empty() ? ImVec2(0, 0)
                                 : measure_text(text.c_str(), TextSize::sm, Weight::medium, 0, font_points(TextSize::sm));
  const float h = std::max(track_h, line);
  const float w = track_w + (text.empty() ? 0 : gap + ts.x);
  const ImRect box = layout::allocate(ImVec2(w, h));
  const ImRect track(ImVec2(box.Min.x, std::floor(box.GetCenter().y - track_h / 2)),
                     ImVec2(box.Min.x + track_w, std::floor(box.GetCenter().y - track_h / 2) + track_h));

  detail::DisabledScope disabled(o.disabled);
  const detail::Interaction in = detail::interact(id, box, o.disabled);
  if (in.pressed) {
    *value = !*value;
    ImGui::MarkItemEdited(id);
  }
  const float on = anim::tween(anim::sub_id(id, "on"), *value ? 1.f : 0.f);

  const Theme &t = theme();
  ImDrawList *dl = ImGui::GetWindowDrawList();
  const float radius = track_h / 2;
  const Color off_bg = t.is_dark() ? t.input.fade(0.8f) : t.input;
  draw::shadow(dl, track, radius, draw::shadow_xs());
  draw::fill(dl, track, mix(off_bg, t.primary, on), radius);
  if (in.focus_t > 0) {
    draw::focus_ring(dl, track, radius, t.ring.fade(in.focus_t));
    draw::border(dl, track, t.ring.fade(in.focus_t), radius, px(1));
  }

  // Thumb: inset by the 1px border, travels track width - thumb - 2px.
  const Color thumb_col = t.is_dark() ? mix(t.foreground, t.primary_foreground, on) : t.background;
  const float travel = track_w - thumb - px(2);
  const ImVec2 c(track.Min.x + px(1) + thumb / 2 + travel * on, track.GetCenter().y);
  dl->AddCircleFilled(c, thumb / 2, thumb_col.u32(), 32);

  if (!text.empty())
    draw_text(dl, ImVec2(track.Max.x + gap, std::floor(box.GetCenter().y - ts.y / 2)), text.c_str(), TextSize::sm,
              Weight::medium, t.foreground, 0, font_points(TextSize::sm));
  return in.pressed;
}

} // namespace silta
