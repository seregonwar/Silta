#include "silta/components/badge.hpp"
#include "common.hpp"
#include <string>

namespace silta {

void Badge(const char *label, const BadgeOptions &o) {
  const Theme &t = theme();
  const std::string text(label, detail::label_end(label));
  // inline-flex rounded-md border px-2 py-0.5 text-xs font-medium gap-1 [&>svg]:size-3
  const ImVec2 ts = measure_text(text.c_str(), TextSize::xs, Weight::medium);
  const float icon_px = px(12), gap = px(4);
  const float content = ts.x + (o.icon != Icon::none ? icon_px + gap : 0);
  const ImRect box = layout::allocate(ImVec2(content + px(16) + px(2), ts.y + px(4) + px(2)));

  Color bg = t.primary, fg = t.primary_foreground, border = transparent;
  switch (o.variant) {
  case Variant::secondary: bg = t.secondary, fg = t.secondary_foreground; break;
  case Variant::destructive:
    bg = t.is_dark() ? t.destructive.fade(0.6f) : t.destructive, fg = t.destructive_foreground;
    break;
  case Variant::outline: bg = transparent, fg = t.foreground, border = t.border; break;
  default: break;
  }
  ImDrawList *dl = ImGui::GetWindowDrawList();
  const float radius = px(t.radius_md());
  draw::fill(dl, box, bg, radius);
  draw::border(dl, box, border, radius, px(1));
  float x = std::floor(box.GetCenter().x - content / 2);
  const float cy = box.GetCenter().y;
  if (o.icon != Icon::none) {
    draw_icon(dl, o.icon, ImVec2(x, std::floor(cy - icon_px / 2)), icon_px, fg);
    x += icon_px + gap;
  }
  draw_text(dl, ImVec2(x, std::floor(cy - ts.y / 2)), text.c_str(), TextSize::xs, Weight::medium, fg);
}

} // namespace silta
