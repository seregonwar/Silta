#pragma once
#include "silta/icons.hpp"

namespace silta {

// shadcn/ui button variants. `primary` is shadcn's "default".
enum class Variant { primary, destructive, outline, secondary, ghost, link };
enum class Size { sm, md, lg, icon };

struct ButtonOptions {
  Variant variant = Variant::primary;
  Size size = Size::md;
  Icon icon = Icon::none;          // before the label (or alone with Size::icon)
  Icon trailing_icon = Icon::none; // after the label
  bool disabled = false;
  bool loading = false; // spinner in place of the icon, input blocked
  float width = 0;      // points, 0 = fit content, fill = available
};

// Returns true when clicked. "Label##id" hides the part after ## from view.
bool Button(const char *label, const ButtonOptions &options = {});

} // namespace silta
