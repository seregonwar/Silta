#pragma once
#include "silta/components/button.hpp"
#include "silta/icons.hpp"

namespace silta {

struct BadgeOptions {
  // primary, secondary, destructive or outline.
  Variant variant = Variant::primary;
  Icon icon = Icon::none;
};

void Badge(const char *label, const BadgeOptions &options = {});

} // namespace silta
