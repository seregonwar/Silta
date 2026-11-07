#include "silta/components/overlay.hpp"
#include "common.hpp"
#include <cfloat>
#include <deque>
#include <string>
#include <unordered_map>

namespace silta {
namespace {
// What an overlay remembers between frames.
struct Memory {
  ImRect anchor;               // trigger rectangle, pixels
  ImVec2 size{0, 0};           // last frame's size
  bool was_open = false;
  bool suppress_reopen = false; // closed by a press on its own trigger
};
std::unordered_map<ImGuiID, Memory> memory;

ImRect viewport_rect() {
  const ImGuiViewport *vp = ImGui::GetMainViewport();
  return {vp->Pos, ImVec2(vp->Pos.x + vp->Size.x, vp->Pos.y + vp->Size.y)};
}

// Top-left corner for a box of `size` placed on `side` of `anchor`, flipped to
// the opposite side when it does not fit and kept inside the viewport.
ImVec2 place(const ImRect &anchor, ImVec2 size, Side side, Align align, float offset, Side *used = nullptr) {
  const ImRect vp = viewport_rect();
  const float margin = px(8);
  auto fits = [&](Side s) {
    switch (s) {
    case Side::bottom: return anchor.Max.y + offset + size.y <= vp.Max.y - margin;
    case Side::top: return anchor.Min.y - offset - size.y >= vp.Min.y + margin;
    case Side::right: return anchor.Max.x + offset + size.x <= vp.Max.x - margin;
    case Side::left: return anchor.Min.x - offset - size.x >= vp.Min.x + margin;
    }
    return true;
  };
  auto opposite = [](Side s) {
    return s == Side::bottom ? Side::top : s == Side::top ? Side::bottom : s == Side::left ? Side::right : Side::left;
  };
  if (!fits(side) && fits(opposite(side))) side = opposite(side);
  if (used) *used = side;

  ImVec2 p;
  const bool vertical = side == Side::top || side == Side::bottom;
  if (vertical) {
    p.y = side == Side::bottom ? anchor.Max.y + offset : anchor.Min.y - offset - size.y;
    p.x = align == Align::end      ? anchor.Max.x - size.x
          : align == Align::center ? anchor.GetCenter().x - size.x / 2
                                   : anchor.Min.x;
  } else {
    p.x = side == Side::right ? anchor.Max.x + offset : anchor.Min.x - offset - size.x;
    p.y = align == Align::end      ? anchor.Max.y - size.y
          : align == Align::center ? anchor.GetCenter().y - size.y / 2
                                   : anchor.Min.y;
  }
  p.x = std::clamp(p.x, vp.Min.x + margin, std::max(vp.Min.x + margin, vp.Max.x - margin - size.x));
  p.y = std::clamp(p.y, vp.Min.y + margin, std::max(vp.Min.y + margin, vp.Max.y - margin - size.y));
  return {std::floor(p.x), std::floor(p.y)};
}

// Where the zoom-in grows from: the edge facing the anchor.
ImVec2 transform_origin(const ImRect &box, Side side) {
  switch (side) {
  case Side::bottom: return {box.GetCenter().x, box.Min.y};
  case Side::top: return {box.GetCenter().x, box.Max.y};
  case Side::right: return {box.Min.x, box.GetCenter().y};
  case Side::left: return {box.Max.x, box.GetCenter().y};
  }
  return box.GetCenter();
}

// shadcn's zoom-in-95: scales everything drawn since `start` around `origin`.
void zoom_vertices(ImDrawList *dl, int start, ImVec2 origin, float scale) {
  if (scale >= 0.999f) return;
  for (int i = start; i < dl->VtxBuffer.Size; ++i) {
    ImVec2 &p = dl->VtxBuffer.Data[i].pos;
    p = ImVec2(origin.x + (p.x - origin.x) * scale, origin.y + (p.y - origin.y) * scale);
  }
}

// Draws an overlay surface (shadow, fill, border) outside the window's clip.
void surface(ImDrawList *dl, const ImRect &box, float radius, Color fill_color, const draw::ShadowStack &shadow) {
  const ImRect vp = viewport_rect();
  dl->PushClipRect(vp.Min, vp.Max, false);
  draw::shadow(dl, box, radius, shadow);
  draw::fill(dl, box, fill_color, radius);
  draw::border(dl, box, theme().border, radius, px(1));
  dl->PopClipRect();
}

void push_window_style() {
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
  ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 0.f);
  ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
}
void pop_window_style() {
  ImGui::PopStyleColor(2);
  ImGui::PopStyleVar(3);
}

// Enter animation progress 0..1 for the current window.
float enter_progress(ImGuiID id, float duration) {
  const ImGuiID key = anim::sub_id(id, "enter");
  if (ImGui::IsWindowAppearing()) anim::reset(key, 0);
  return anim::tween(key, 1.f, duration, anim::Ease::ease_out);
}

constexpr ImGuiWindowFlags overlay_flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                                           ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground;
} // namespace

// ============================================================== Popover
struct Popover::State {
  ImGuiID id = 0;
  std::optional<layout::Isolate> isolate;
  std::optional<Flex> content;
  ImDrawList *dl = nullptr;
  int vtx_start = 0;
  ImVec2 origin;
  float t = 1;
};
namespace {
std::deque<Popover::State> popover_states;
}

void OpenPopover(const char *sid) {
  const ImGuiID id = ImGui::GetID(sid);
  Memory &m = memory[id];
  m.anchor = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
  if (m.suppress_reopen) { // this press closed it: a second click toggles off
    m.suppress_reopen = false;
    return;
  }
  ImGui::OpenPopupEx(id, ImGuiPopupFlags_NoReopen);
}

void ClosePopover() { ImGui::CloseCurrentPopup(); }

bool IsPopoverOpen(const char *sid) { return ImGui::IsPopupOpen(ImGui::GetID(sid), ImGuiPopupFlags_None); }

Popover::Popover(const char *sid, const PopoverOptions &o) {
  const ImGuiID id = ImGui::GetID(sid);
  Memory &m = memory[id];
  if (!ImGui::IsMouseDown(0) && !ImGui::IsMouseReleased(0)) m.suppress_reopen = false;
  if (!ImGui::IsPopupOpen(id, ImGuiPopupFlags_None)) {
    // Closed by the press that will also "click" the trigger: do not reopen.
    if (m.was_open && ImGui::IsMouseDown(0) && m.anchor.Contains(ImGui::GetMousePos())) m.suppress_reopen = true;
    m.was_open = false;
    return;
  }
  m.was_open = true;

  Side side = o.side;
  const ImVec2 pos = place(m.anchor, m.size, o.side, o.align, px(o.offset), &side);
  ImGui::SetNextWindowPos(pos);
  if (o.width > 0) ImGui::SetNextWindowSizeConstraints(ImVec2(px(o.width), 0), ImVec2(px(o.width), FLT_MAX));
  else if (o.min_width > 0) ImGui::SetNextWindowSizeConstraints(ImVec2(px(o.min_width), 0), ImVec2(FLT_MAX, FLT_MAX));
  push_window_style();
  open_ = ImGui::BeginPopupEx(id, overlay_flags);
  pop_window_style();
  if (!open_) return;

  State &s = popover_states.emplace_back();
  state_ = &s;
  s.id = id;
  s.isolate.emplace();
  s.t = enter_progress(id, 0.15f);
  ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * s.t);

  ImGuiWindow *window = ImGui::GetCurrentWindow();
  s.dl = window->DrawList;
  s.vtx_start = s.dl->VtxBuffer.Size;
  const ImRect box = window->Rect();
  s.origin = transform_origin(box, side);
  if (box.GetWidth() > 0 && box.GetHeight() > 0)
    surface(s.dl, box, px(theme().radius_md()), theme().popover, draw::shadow_md());
  s.content.emplace(FlexOptions{.axis = Axis::column, .gap = o.gap, .padding = Edges::all(o.padding),
                                .align = Align::stretch, .width = o.width > 0 ? fill : 0.f});
  // A minimum width (menus, select lists) widens the content, so full-width
  // rows reach the edges.
  if (o.width <= 0 && o.min_width > 0) layout::allocate(ImVec2(px(o.min_width) - 2 * px(o.padding), 0));
}

Popover::~Popover() {
  if (!open_) return;
  State &s = *state_;
  s.content.reset();
  s.isolate.reset();
  zoom_vertices(s.dl, s.vtx_start, s.origin, 0.95f + 0.05f * s.t);
  ImGui::PopStyleVar();
  memory[s.id].size = ImGui::GetWindowSize();
  ImGui::EndPopup();
  popover_states.pop_back();
}

// ========================================================== DropdownMenu
DropdownMenu::DropdownMenu(const char *id, const MenuOptions &o)
    : popover_(id, PopoverOptions{.width = 0, .min_width = o.min_width, .side = o.side, .align = o.align,
                                  .offset = 4, .padding = 4}) {}

namespace {
bool menu_row(const char *label, const MenuItemOptions &o, bool close_on_press) {
  const Theme &t = theme();
  const ImGuiID id = ImGui::GetID(label);
  const std::string text(label, detail::label_end(label));
  const float pad = px(8), h = px(32), icon = px(16), gap = px(8);
  const float lead = (o.checked ? icon + gap : 0) + (o.icon != Icon::none ? icon + gap : 0);
  const ImVec2 ts = measure_text(text.c_str(), TextSize::sm);
  const float shortcut_w = o.shortcut ? measure_text(o.shortcut, TextSize::xs).x + px(24) : 0;
  const float natural = pad * 2 + lead + ts.x + shortcut_w;
  const ImRect box = layout::allocate(ImVec2(std::max(natural, layout::fill_width()), h));

  detail::DisabledScope disabled(o.disabled);
  const detail::Interaction in = detail::interact(id, box, o.disabled);
  const bool focused = !o.disabled && (in.hovered || (GImGui->NavId == id && GImGui->NavCursorVisible));
  ImDrawList *dl = ImGui::GetWindowDrawList();
  if (focused) draw::fill(dl, box, o.destructive ? t.destructive.fade(0.1f) : t.accent, px(t.radius_sm()));
  const Color ink = o.destructive ? t.destructive : focused ? t.accent_foreground : t.popover_foreground;
  const float cy = box.GetCenter().y;
  float x = box.Min.x + pad;
  if (o.checked) {
    if (*o.checked) draw_icon(dl, Icon::check, ImVec2(x, std::floor(cy - icon / 2)), icon, ink);
    x += icon + gap;
  }
  if (o.icon != Icon::none) {
    draw_icon(dl, o.icon, ImVec2(x, std::floor(cy - icon / 2)), icon, o.destructive ? t.destructive : t.muted_foreground);
    x += icon + gap;
  }
  draw_text(dl, ImVec2(x, std::floor(cy - ts.y / 2)), text.c_str(), TextSize::sm, Weight::regular, ink);
  if (o.shortcut) {
    const ImVec2 ss = measure_text(o.shortcut, TextSize::xs);
    draw_text(dl, ImVec2(box.Max.x - pad - ss.x, std::floor(cy - ss.y / 2)), o.shortcut, TextSize::xs,
              Weight::regular, t.muted_foreground);
  }
  if (in.pressed && close_on_press) ClosePopover();
  return in.pressed;
}
} // namespace

bool MenuItem(const char *label, const MenuItemOptions &options) { return menu_row(label, options, true); }

bool MenuCheckbox(const char *label, bool *value) {
  MenuItemOptions o;
  o.checked = value;
  if (!menu_row(label, o, false)) return false;
  *value = !*value;
  return true;
}

void MenuLabel(const char *text) {
  const ImVec2 ts = measure_text(text, TextSize::sm, Weight::medium);
  const ImRect box = layout::allocate(ImVec2(std::max(ts.x + px(16), layout::fill_width()), px(32)));
  draw_text(ImGui::GetWindowDrawList(), ImVec2(box.Min.x + px(8), std::floor(box.GetCenter().y - ts.y / 2)), text,
            TextSize::sm, Weight::medium, theme().popover_foreground);
}

void MenuSeparator() {
  const ImRect box = layout::allocate(ImVec2(layout::fill_width(), px(9)));
  const float y = std::floor(box.GetCenter().y);
  ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(box.Min.x - px(4), y), ImVec2(box.Max.x + px(4), y + px(1)),
                                            theme().border.u32());
}

// ================================================================ Select
bool Select(const char *sid, int *selected, std::span<const char *const> items, const SelectOptions &o) {
  const Theme &t = theme();
  ImGui::PushID(sid);
  const ImGuiID trigger = ImGui::GetID("##trigger");
  float widest = measure_text(o.placeholder ? o.placeholder : "", TextSize::sm).x;
  for (const char *item : items) widest = std::max(widest, measure_text(item, TextSize::sm).x);
  const float h = px(o.small ? 32.f : 36.f), icon = px(16);
  const float natural = px(12) * 2 + widest + px(8) + icon;
  const float w = layout::resolve_width(o.width, natural);
  const ImRect box = layout::allocate(ImVec2(w, h), o.width == fill);

  bool changed = false;
  {
    detail::DisabledScope disabled(o.disabled);
    const detail::Interaction in = detail::interact(trigger, box, o.disabled);
    ImDrawList *dl = ImGui::GetWindowDrawList();
    const float radius = px(t.radius_md());
    draw::shadow(dl, box, radius, draw::shadow_xs());
    if (t.is_dark()) draw::fill(dl, box, mix(t.input.fade(0.3f), t.input.fade(0.5f), in.hover_t), radius);
    draw::border(dl, box, mix(t.input, t.ring, in.focus_t), radius, px(1));
    if (in.focus_t > 0) draw::focus_ring(dl, box, radius, t.ring.fade(in.focus_t));
    const bool has_value = *selected >= 0 && *selected < int(items.size());
    const char *shown = has_value ? items[size_t(*selected)] : (o.placeholder ? o.placeholder : "");
    const ImVec2 ts = measure_text(shown, TextSize::sm);
    dl->PushClipRect(box.Min, ImVec2(box.Max.x - px(12) - icon - px(4), box.Max.y), true);
    draw_text(dl, ImVec2(box.Min.x + px(12), std::floor(box.GetCenter().y - ts.y / 2)), shown, TextSize::sm,
              Weight::regular, has_value ? t.foreground : t.muted_foreground);
    dl->PopClipRect();
    draw_icon(dl, Icon::chevron_down, ImVec2(box.Max.x - px(12) - icon, std::floor(box.GetCenter().y - icon / 2)),
              icon, t.muted_foreground.fade(0.5f));
    if (in.pressed) OpenPopover("##list");
  }

  if (auto pop = Popover("##list", {.width = 0, .min_width = w / scale(), .side = Side::bottom,
                                    .align = Align::start, .offset = 4, .padding = 4})) {
    for (int i = 0; i < int(items.size()); ++i) {
      ImGui::PushID(i);
      const ImGuiID id = ImGui::GetID("##item");
      const ImVec2 ts = measure_text(items[size_t(i)], TextSize::sm);
      const ImRect row = layout::allocate(ImVec2(std::max(px(8) + ts.x + px(32), layout::fill_width()), px(32)));
      const detail::Interaction in = detail::interact(id, row, false);
      const bool focused = in.hovered || (GImGui->NavId == id && GImGui->NavCursorVisible);
      ImDrawList *dl = ImGui::GetWindowDrawList();
      if (focused) draw::fill(dl, row, t.accent, px(t.radius_sm()));
      const float cy = row.GetCenter().y;
      draw_text(dl, ImVec2(row.Min.x + px(8), std::floor(cy - ts.y / 2)), items[size_t(i)], TextSize::sm,
                Weight::regular, focused ? t.accent_foreground : t.popover_foreground);
      if (i == *selected)
        draw_icon(dl, Icon::check, ImVec2(row.Max.x - px(8) - icon, std::floor(cy - icon / 2)), icon,
                  t.popover_foreground);
      if (in.pressed) {
        changed = *selected != i;
        *selected = i;
        ClosePopover();
      }
      ImGui::PopID();
    }
  }
  ImGui::PopID();
  return changed;
}

// =============================================================== Tooltip
void Tooltip(const char *text, const TooltipOptions &o) {
  static ImGuiID current = 0;
  static double since = 0;
  const ImGuiContext &g = *GImGui;
  const ImGuiID item = g.LastItemData.ID;
  const bool hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);
  const bool focused = item != 0 && g.NavId == item && g.NavCursorVisible;
  if (!hovered && !focused) {
    if (current == item) current = 0;
    return;
  }
  const double now = ImGui::GetTime();
  if (current != item) current = item, since = now;
  const double waited = focused && !hovered ? o.delay : now - since;
  if (waited < o.delay) {
    wake_in(o.delay - waited);
    return;
  }

  const Theme &t = theme();
  const ImRect anchor(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
  const ImVec2 ts = measure_text(text, TextSize::xs);
  const ImVec2 size(ts.x + px(24), ts.y + px(12)); // px-3 py-1.5
  const float arrow = px(5);
  Side side = o.side;
  const ImVec2 pos = place(anchor, size, o.side, Align::center, arrow + px(2), &side);

  layout::Isolate isolate;
  ImGui::SetNextWindowPos(pos);
  ImGui::SetNextWindowSize(size);
  push_window_style();
  ImGui::Begin("##silta.tooltip", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings |
                   ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoBackground |
                   ImGuiWindowFlags_Tooltip);
  pop_window_style();
  const float tt = enter_progress(ImGui::GetID("##silta.tooltip"), 0.15f);
  ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * tt);
  ImDrawList *dl = ImGui::GetWindowDrawList();
  const int start = dl->VtxBuffer.Size;
  const ImRect box(pos, ImVec2(pos.x + size.x, pos.y + size.y));
  const ImRect vp = viewport_rect();
  dl->PushClipRect(vp.Min, vp.Max, false);
  draw::fill(dl, box, t.foreground, px(t.radius_md()));
  // Arrow pointing at the anchor.
  const float ax = std::clamp(anchor.GetCenter().x, box.Min.x + px(10), box.Max.x - px(10));
  const float ay = std::clamp(anchor.GetCenter().y, box.Min.y + px(10), box.Max.y - px(10));
  const ImU32 col = t.foreground.u32();
  switch (side) {
  case Side::top: dl->AddTriangleFilled(ImVec2(ax - arrow, box.Max.y), ImVec2(ax + arrow, box.Max.y), ImVec2(ax, box.Max.y + arrow), col); break;
  case Side::bottom: dl->AddTriangleFilled(ImVec2(ax - arrow, box.Min.y), ImVec2(ax, box.Min.y - arrow), ImVec2(ax + arrow, box.Min.y), col); break;
  case Side::left: dl->AddTriangleFilled(ImVec2(box.Max.x, ay - arrow), ImVec2(box.Max.x + arrow, ay), ImVec2(box.Max.x, ay + arrow), col); break;
  case Side::right: dl->AddTriangleFilled(ImVec2(box.Min.x, ay - arrow), ImVec2(box.Min.x, ay + arrow), ImVec2(box.Min.x - arrow, ay), col); break;
  }
  draw_text(dl, ImVec2(box.Min.x + px(12), box.Min.y + px(6)), text, TextSize::xs, Weight::regular, t.background);
  dl->PopClipRect();
  zoom_vertices(dl, start, transform_origin(box, side), 0.95f + 0.05f * tt);
  ImGui::PopStyleVar();
  ImGui::End();
}

// ================================================================ Dialog
struct Dialog::State {
  ImGuiID id = 0;
  std::optional<layout::Isolate> isolate;
  std::optional<Flex> content;
  ImDrawList *dl = nullptr;
  int vtx_start = 0;
  ImVec2 origin;
  float t = 1;
};
namespace {
std::deque<Dialog::State> dialog_states;
}

void OpenDialog(const char *id) { ImGui::OpenPopup(id); }

Dialog::Dialog(const char *sid, const DialogOptions &o) {
  const ImGuiID id = ImGui::GetID(sid);
  if (!ImGui::IsPopupOpen(id, ImGuiPopupFlags_None)) return;
  const Theme &t = theme();
  const ImGuiViewport *vp = ImGui::GetMainViewport();
  const float w = std::min(px(o.width), vp->Size.x - px(32));
  ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSizeConstraints(ImVec2(w, 0), ImVec2(w, vp->Size.y - px(32)));
  push_window_style();
  open_ = ImGui::BeginPopupModal(sid, nullptr, overlay_flags);
  pop_window_style();
  if (!open_) return;

  State &s = dialog_states.emplace_back();
  state_ = &s;
  s.id = id;
  s.isolate.emplace();
  s.t = enter_progress(id, 0.2f);
  ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * s.t);
  ImGuiWindow *window = ImGui::GetCurrentWindow();
  s.dl = window->DrawList;
  s.vtx_start = s.dl->VtxBuffer.Size;
  const ImRect box = window->Rect();
  s.origin = box.GetCenter();
  const float radius = px(t.radius_lg());
  if (box.GetWidth() > 0 && box.GetHeight() > 0) surface(s.dl, box, radius, t.background, draw::shadow_lg());

  // Dismissal: Escape (unless a field is being edited) and a click outside.
  if (o.dismissable && !ImGui::IsWindowAppearing()) {
    const bool escape = ImGui::IsKeyPressed(ImGuiKey_Escape, false) && ImGui::GetActiveID() == 0;
    const bool outside = ImGui::IsMouseClicked(0) && !box.Contains(ImGui::GetMousePos());
    if (escape || outside) ImGui::CloseCurrentPopup();
  }
  // Close button: top-4 right-4, opacity-70 until hovered.
  if (o.close_button) {
    const float icon = px(16);
    const ImRect x(ImVec2(box.Max.x - px(16) - icon, box.Min.y + px(16)), ImVec2(box.Max.x - px(16), box.Min.y + px(16) + icon));
    const float slop = px(4);
    const ImRect hit(ImVec2(x.Min.x - slop, x.Min.y - slop), ImVec2(x.Max.x + slop, x.Max.y + slop));
    const detail::Interaction in = detail::interact(ImGui::GetID("##close"), hit, false);
    if (in.focus_t > 0) draw::focus_ring(s.dl, x, px(2), t.ring.fade(in.focus_t));
    draw_icon(s.dl, Icon::x, x.Min, icon, t.foreground.fade(0.7f + 0.3f * in.hover_t));
    if (in.pressed) ImGui::CloseCurrentPopup();
  }

  s.content.emplace(FlexOptions{.axis = Axis::column, .gap = 16, .padding = Edges::all(24), .align = Align::stretch,
                                .width = fill});
  if (o.title || o.description) {
    auto header = Column({.gap = 8});
    if (o.title) Text(o.title, {.size = TextSize::lg, .weight = Weight::semibold, .line_height = 18});
    if (o.description) Text(o.description, {.wrap = true, .muted = true});
  }
}

Dialog::~Dialog() {
  if (!open_) return;
  State &s = *state_;
  s.content.reset();
  s.isolate.reset();
  zoom_vertices(s.dl, s.vtx_start, s.origin, 0.95f + 0.05f * s.t);
  ImGui::PopStyleVar();
  ImGui::EndPopup();
  dialog_states.pop_back();
}

} // namespace silta
