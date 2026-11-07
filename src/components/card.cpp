#include "silta/components/card.hpp"
#include "common.hpp"

namespace silta {
namespace {
FlexOptions card_flex(const CardOptions &o) {
  // flex flex-col gap-6 rounded-xl border py-6 shadow-sm
  return {.axis = Axis::column, .gap = 24, .padding = Edges::y(24), .align = Align::stretch,
          .width = o.width, .height = o.height, .id = o.id};
}

FlexOptions section(const SectionOptions &o, Axis axis, float default_gap, Align default_align) {
  return {.axis = axis, .gap = o.gap >= 0 ? o.gap : default_gap, .padding = Edges::x(24),
          .align = o.align.value_or(default_align), .justify = o.justify};
}
} // namespace

Card::Card(const CardOptions &options) : flex_(card_flex(options)) {
  const ImRect box = flex_.rect();
  if (box.GetWidth() <= 0 || box.GetHeight() <= 0) return; // measured next frame
  const Theme &t = theme();
  const float radius = px(t.radius_xl());
  ImDrawList *dl = ImGui::GetWindowDrawList();
  draw::shadow(dl, box, radius, draw::shadow_sm());
  draw::fill(dl, box, t.card, radius);
  draw::border(dl, box, t.border, radius, px(1));
}

CardHeader::CardHeader(const SectionOptions &o) : flex_(section(o, Axis::column, 6, Align::stretch)) {}
CardContent::CardContent(const SectionOptions &o) : flex_(section(o, Axis::column, 0, Align::stretch)) {}
CardFooter::CardFooter(const SectionOptions &o) : flex_(section(o, Axis::row, 0, Align::center)) {}

void CardTitle(const char *text) {
  // leading-none font-semibold
  Text(text, {.size = TextSize::base, .weight = Weight::semibold, .color = theme().card_foreground,
              .line_height = font_points(TextSize::base)});
}

void CardDescription(const char *text) {
  Text(text, {.size = TextSize::sm, .wrap = true, .muted = true});
}

} // namespace silta
