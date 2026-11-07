#pragma once

namespace silta {

struct SeparatorOptions {
  bool vertical = false;
};

// A 1px rule in the border color, across the available width (or height).
void Separator(const SeparatorOptions &options = {});

} // namespace silta
