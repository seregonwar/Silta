#include "silta/components/toast.hpp"
#include "silta/icons.hpp"
#include "common.hpp"
#include <string>
#include <vector>

namespace silta {
namespace {
struct Entry {
  std::string title, description;
  ToastKind kind;
  double created;
  float duration;
  ImGuiID id;
};
std::vector<Entry> toasts;
unsigned next_id = 1;

Icon icon_for(ToastKind k) {
  switch (k) {
  case ToastKind::success: return Icon::check;
  case ToastKind::info: return Icon::info;
  case ToastKind::warning:
  case ToastKind::error: return Icon::alert;
  default: return Icon::none;
  }
}
Color color_for(ToastKind k, const Theme &t) {
  switch (k) {
  case ToastKind::success: return Color::oklch(0.627f, 0.194f, 149.214f); // green-600
  case ToastKind::info: return Color::oklch(0.546f, 0.245f, 262.881f);    // blue-600
  case ToastKind::warning: return Color::oklch(0.666f, 0.179f, 58.318f);  // amber-600
  case ToastKind::error: return t.destructive;
  default: return t.foreground;
  }
}
} // namespace

void toast(const char *title, const ToastOptions &o) {
  if (!ImGui::GetCurrentContext()) return;
  toasts.push_back({title ? title : "", o.description ? o.description : "", o.kind, ImGui::GetTime(), o.duration,
                    ImHashData(&next_id, sizeof next_id, 0x7057)});
  anim::reset(anim::sub_id(toasts.back().id, "in"), 0.f); // slide in from below
  ++next_id;
  request_frames(2);
}

void Toaster() {
  const double now = ImGui::GetTime();
  const float fade = 0.2f;
  // Drop the ones whose exit animation is over.
  std::erase_if(toasts, [&](const Entry &e) { return now - e.created > e.duration + fade; });
  if (toasts.empty()) return;

  const Theme &t = theme();
  const ImGuiViewport *vp = ImGui::GetMainViewport();
  ImDrawList *dl = ImGui::GetForegroundDrawList();
  const float width = std::min(px(356), vp->Size.x - px(32));
  const float margin = px(24), gap = px(12), pad = px(16), icon = px(16);
  float bottom = vp->Pos.y + vp->Size.y - margin;
  double next_change = 1e9;
  // Newest at the bottom, older ones stacked above.
  for (auto it = toasts.rbegin(); it != toasts.rend(); ++it) {
    const Entry &e = *it;
    const double age = now - e.created;
    const bool leaving = age > e.duration;
    const float in = anim::tween(anim::sub_id(e.id, "in"), 1.f, 0.25f, anim::Ease::ease_out);
    const float out = leaving ? std::clamp(float((age - e.duration) / fade), 0.f, 1.f) : 0.f;
    const float alpha = in * (1 - out);
    next_change = std::min(next_change, leaving ? 0.0 : e.duration - age);

    const float text_x = pad + (icon_for(e.kind) != Icon::none ? icon + px(10) : 0);
    const float wrap = width - text_x - pad;
    const ImVec2 title = measure_text(e.title.c_str(), TextSize::sm, Weight::medium, wrap);
    const ImVec2 desc = e.description.empty() ? ImVec2(0, 0) : measure_text(e.description.c_str(), TextSize::sm, Weight::regular, wrap);
    const float h = pad * 2 + title.y + (desc.y > 0 ? px(2) + desc.y : 0);
    const float slide = (1 - in) * px(16);
    const float x = vp->Pos.x + vp->Size.x - margin - width;
    const ImRect box(ImVec2(x, bottom - h + slide), ImVec2(x + width, bottom + slide));

    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
    const float radius = px(t.radius_lg());
    draw::shadow(dl, box, radius, draw::shadow_lg());
    draw::fill(dl, box, t.popover, radius);
    draw::border(dl, box, t.border, radius, px(1));
    if (icon_for(e.kind) != Icon::none)
      draw_icon(dl, icon_for(e.kind), ImVec2(box.Min.x + pad, box.Min.y + pad + px(2)), icon, color_for(e.kind, t));
    draw_text(dl, ImVec2(box.Min.x + text_x, box.Min.y + pad), e.title.c_str(), TextSize::sm, Weight::medium,
              t.popover_foreground, wrap);
    if (desc.y > 0)
      draw_text(dl, ImVec2(box.Min.x + text_x, box.Min.y + pad + title.y + px(2)), e.description.c_str(),
                TextSize::sm, Weight::regular, t.muted_foreground, wrap);
    ImGui::PopStyleVar();
    bottom -= (h + gap) * alpha + 0.f;
  }
  // Wake up when the next toast starts leaving; animate while it does.
  if (next_change <= 0) request_frames(1);
  else wake_in(next_change);
}

} // namespace silta
