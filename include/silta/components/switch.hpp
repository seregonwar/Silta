#pragma once

namespace silta {

struct SwitchOptions {
  bool disabled = false;
};

// A toggle with its label on the right; clicking the label toggles too.
// "##id" alone gives a switch without a label. Returns true when toggled.
bool Switch(const char *label, bool *value, const SwitchOptions &options = {});

} // namespace silta
