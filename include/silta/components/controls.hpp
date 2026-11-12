#pragma once
// Form controls and small pieces: Checkbox, Slider, Tabs, Toggle,
// ToggleGroup, Progress, Kbd.
#include <span>
#include "silta/components/button.hpp"
#include "silta/icons.hpp"
#include "silta/layout.hpp"

namespace silta {

struct CheckboxOptions {
  bool disabled = false;
};
// A checkbox with its label after the box (right in LTR, left in RTL);
// "##id" for none. True when toggled.
bool Checkbox(const char *label, bool *value, const CheckboxOptions &options = {});

struct SliderOptions {
  float step = 0;      // 0 = continuous; keyboard moves by step or 1%
  float width = fill;  // points or fill
  bool disabled = false;
};
// Drag the thumb, click the track, or use arrows / Home / End when focused.
// In RTL the track starts at the right and the arrows follow it.
bool Slider(const char *id, float *value, float min, float max, const SliderOptions &options = {});

struct TabsOptions {
  float width = 0; // points; 0 = fit the labels, fill = equal tabs across
};
// shadcn TabsList + TabsTrigger. *active is the selected index. True on change.
bool Tabs(const char *id, int *active, std::span<const char *const> labels, const TabsOptions &options = {});

struct ToggleOptions {
  bool outline = false;
  Size size = Size::md; // sm, md, lg
  Icon icon = Icon::none;
  bool disabled = false;
};
// A two-state button. "##id" with an icon gives an icon-only toggle.
bool Toggle(const char *label, bool *on, const ToggleOptions &options = {});

struct ToggleGroupItem {
  const char *label = nullptr; // nullptr or "" for icon only
  Icon icon = Icon::none;
  const char *tooltip = nullptr;
};
// Joined toggles where exactly one is on (*value is its index). True on change.
bool ToggleGroup(const char *id, int *value, std::span<const ToggleGroupItem> items, const ToggleOptions &options = {});

struct ProgressOptions {
  float width = fill; // points or fill
};
// value in [0, 1], animated.
void Progress(const char *id, float value, const ProgressOptions &options = {});

// A keyboard key or shortcut: Kbd("Ctrl+K").
void Kbd(const char *keys);

} // namespace silta
