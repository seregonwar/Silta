#include "silta/components/separator.hpp"
#include "common.hpp"

namespace silta {

void Separator(const SeparatorOptions &o) {
  const float one = std::max(1.f, std::floor(px(1)));
  const ImVec2 size = o.vertical ? ImVec2(one, layout::fill_height()) : ImVec2(layout::fill_width(), one);
  const ImRect r = layout::allocate(size);
  draw::fill(ImGui::GetWindowDrawList(), r, theme().border, 0);
}

} // namespace silta
