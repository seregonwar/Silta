#include "silta/components/button.hpp"
#include "common.hpp"
#include <string>

namespace silta {
namespace {
struct Metrics {
  float height, pad_x, pad_x_with_icon, gap;
};
Metrics metrics(Size size) {
  switch (size) {
  case Size::sm: return {32, 12, 10, 6};
  case Size::lg: return {40, 24, 16, 8};
  case Size::icon: return {36, 0, 0, 0};
  case Size::md: break;
  }
  return {36, 16, 12, 8};
}

struct Look {
  Color bg, bg_hover, fg, fg_hover, border = transparent, border_hover = transparent;
  bool shadow = true;
};

Look look(Variant v, const Theme &t) {
  const bool dark = t.is_dark();
  switch (v) {
  case Variant::primary:
    return {t.primary, t.primary.fade(0.9f), t.primary_foreground, t.primary_foreground};
  case Variant::destructive: {
    const Color bg = dark ? t.destructive.fade(0.6f) : t.destructive;
    return {bg, bg.fade(0.9f), t.destructive_foreground, t.destructive_foreground};
  }
  case Variant::outline:
    if (dark)
      return {t.input.fade(0.3f), t.input.fade(0.5f), t.foreground, t.accent_foreground, t.input, t.input};
    return {t.background, t.accent, t.foreground, t.accent_foreground, t.border, t.border};
  case Variant::secondary:
    return {t.secondary, t.secondary.fade(0.8f), t.secondary_foreground, t.secondary_foreground};
  case Variant::ghost:
    return {transparent, dark ? t.accent.fade(0.5f) : t.accent, t.foreground, t.accent_foreground,
            transparent, transparent, false};
  case Variant::link:
    return {transparent, transparent, t.primary, t.primary, transparent, transparent, false};
  }
  return {};
}
} // namespace

bool Button(const char *label, const ButtonOptions &o) {
  const ImGuiID id = ImGui::GetID(label);
  const std::string text(label, detail::label_end(label));
  const Metrics m = metrics(o.size);
  const Icon lead = o.loading ? Icon::loader : o.icon;
  const bool has_icon = lead != Icon::none || o.trailing_icon != Icon::none;

  // Size: padding + [icon] + gap + label + gap + [icon] + padding.
  const float icon_px = px(16);
  const float gap = px(m.gap);
  const ImVec2 text_size = text.empty() ? ImVec2(0, 0) : measure_text(text.c_str(), TextSize::sm, Weight::medium);
  float content = text_size.x;
  int parts = text.empty() ? 0 : 1;
  if (lead != Icon::none) content += icon_px, ++parts;
  if (o.trailing_icon != Icon::none) content += icon_px, ++parts;
  content += gap * float(std::max(0, parts - 1));
  const float pad = px(has_icon ? m.pad_x_with_icon : m.pad_x);
  const float h = px(m.height);
  const float natural_w = o.size == Size::icon ? h : content + 2 * pad;
  // Icon buttons have an explicit size (size-9): they never stretch.
  const float w = o.size == Size::icon && o.width == 0 ? natural_w : layout::resolve_width(o.width, natural_w);
  const ImRect box = layout::allocate(ImVec2(w, h), o.width == fill);

  detail::DisabledScope disabled(o.disabled || o.loading);
  const detail::Interaction in = detail::interact(id, box, o.disabled || o.loading);

  const Theme &t = theme();
  const Look lk = look(o.variant, t);
  const float radius = px(t.radius_md());
  ImDrawList *dl = ImGui::GetWindowDrawList();

  if (lk.shadow) draw::shadow(dl, box, radius, draw::shadow_xs());
  draw::fill(dl, box, mix(lk.bg, lk.bg_hover, in.hover_t), radius);
  Color border = mix(lk.border, lk.border_hover, in.hover_t);
  if (in.focus_t > 0) {
    border = mix(border, t.ring, in.focus_t);
    draw::focus_ring(dl, box, radius, t.ring.fade(in.focus_t));
  }
  draw::border(dl, box, border, radius, px(1));

  // Content, centred.
  const Color fg = mix(lk.fg, lk.fg_hover, in.hover_t);
  float x = std::floor(box.GetCenter().x - content / 2);
  const float cy = box.GetCenter().y;
  if (lead != Icon::none) {
    draw_icon(dl, lead, ImVec2(x, std::floor(cy - icon_px / 2)), icon_px, fg);
    x += icon_px + gap;
  }
  if (!text.empty()) {
    const ImVec2 at(x, std::floor(cy - text_size.y / 2));
    draw_text(dl, at, text.c_str(), TextSize::sm, Weight::medium, fg);
    if (o.variant == Variant::link && in.hovered) {
      const float y = std::floor(at.y + text_size.y - px(2));
      dl->AddLine(ImVec2(at.x, y), ImVec2(at.x + text_size.x, y), fg.u32(), px(1));
    }
    x += text_size.x + gap;
  }
  if (o.trailing_icon != Icon::none) draw_icon(dl, o.trailing_icon, ImVec2(x, std::floor(cy - icon_px / 2)), icon_px, fg);

  if (o.variant == Variant::link && in.hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
  return in.pressed;
}

} // namespace silta
