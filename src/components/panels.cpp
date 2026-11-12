#include "silta/components/panels.hpp"
#include "common.hpp"
#include "../per_context.hpp"
#include <unordered_map>

namespace silta {

// ============================================================ ScrollArea
ScrollArea::ScrollArea(const char *id, const ScrollAreaOptions &o) {
  const ImVec2 size(layout::resolve_width(o.width, px(240)), layout::resolve_height(o.height, px(240)));
  const ImRect box = layout::allocate(size, o.width == fill);
  saved_cursor_ = ImGui::GetCursorScreenPos();
  ImGui::SetCursorScreenPos(box.Min);
  // shadcn scroll-area: a thin, rounded thumb in the border colour.
  const Theme &t = theme();
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
  ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, px(10));
  ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarRounding, px(5));
  ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.f);
  ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, ImVec4(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, t.border.vec4());
  ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered, mix(t.border, t.muted_foreground, 0.4f).vec4());
  ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabActive, t.muted_foreground.vec4());
  ImGui::BeginChild(id, box.GetSize(), ImGuiChildFlags_None,
                    o.horizontal ? ImGuiWindowFlags_HorizontalScrollbar : ImGuiWindowFlags_None);
  ImGui::PopStyleColor(5);
  ImGui::PopStyleVar(4);
  isolate_.emplace();
  content_.emplace(FlexOptions{.axis = Axis::column, .gap = o.gap, .padding = o.padding, .align = Align::stretch,
                               .width = o.horizontal ? 0.f : fill});
}

ScrollArea::~ScrollArea() {
  content_.reset();
  isolate_.reset();
  ImGui::EndChild();
  // The layout already placed the area; undo ImGui's own cursor advance.
  ImGui::SetCursorScreenPos(saved_cursor_);
}

// ================================================================= Split
namespace {
// First pane's share, by split id.
std::unordered_map<ImGuiID, float> &split_share() { return detail::per_context<std::unordered_map<ImGuiID, float>>(); }
}

Split::Split(const char *id, const SplitOptions &o)
    : options_(o), id_(ImGui::GetID(id)),
      outer_(FlexOptions{.axis = o.axis, .width = o.width, .height = o.height, .id = id}) {
  const ImRect box = outer_.rect();
  const float total = o.axis == Axis::row ? box.GetWidth() : box.GetHeight();
  auto [it, inserted] = split_share().try_emplace(id_, o.initial);
  float share = it->second;
  const float lo = px(o.min_first), hi = std::max(lo, total - px(o.min_second) - px(1));
  first_px_ = std::floor(std::clamp(total * share, lo, hi));
  (void)inserted;
}

Flex Split::first(FlexOptions o) {
  if (options_.axis == Axis::row) o.width = first_px_ / scale(), o.height = fill;
  else o.height = first_px_ / scale(), o.width = fill;
  return Flex(o);
}

Flex Split::second(FlexOptions o) {
  const Theme &t = theme();
  const bool row = options_.axis == Axis::row;
  // The 1px divider, with a wider grab area.
  const ImRect line = layout::allocate(row ? ImVec2(px(1), layout::fill_height()) : ImVec2(layout::fill_width(), px(1)));
  const float slop = px(4);
  const ImRect hit = row ? ImRect(ImVec2(line.Min.x - slop, line.Min.y), ImVec2(line.Max.x + slop, line.Max.y))
                         : ImRect(ImVec2(line.Min.x, line.Min.y - slop), ImVec2(line.Max.x, line.Max.y + slop));
  const ImGuiID handle = ImGui::GetID("##split.handle");
  const detail::Interaction in = detail::interact(handle, hit, false);
  if (in.hovered || in.held) ImGui::SetMouseCursor(row ? ImGuiMouseCursor_ResizeEW : ImGuiMouseCursor_ResizeNS);
  const ImRect box = outer_.rect();
  const float total = row ? box.GetWidth() : box.GetHeight();
  if (in.held && total > 0) {
    const float mouse = row ? (is_rtl() ? box.Max.x - ImGui::GetIO().MousePos.x
                                        : ImGui::GetIO().MousePos.x - box.Min.x)
                            : ImGui::GetIO().MousePos.y - box.Min.y;
    split_share()[id_] = std::clamp(mouse / total, 0.f, 1.f);
    request_frames(1);
  }
  ImDrawList *dl = ImGui::GetWindowDrawList();
  const Color color = mix(t.border, t.ring, std::max(in.hover_t, in.held ? 1.f : 0.f));
  draw::fill(dl, line, color, 0);

  if (row) o.width = fill, o.height = fill;
  else o.height = fill, o.width = fill;
  return Flex(o);
}

} // namespace silta
