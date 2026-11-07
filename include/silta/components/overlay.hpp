#pragma once
// Floating layers: Popover, DropdownMenu, Select, Tooltip, Dialog.
//
//   if (Button("Open")) OpenPopover("filters");      // anchored to the button
//   if (auto pop = Popover("filters", {.width = 320})) { ... }
//
//   if (Button("Edit profile")) OpenDialog("profile");
//   if (auto dlg = Dialog("profile", {.title = "Edit profile"})) {
//     ...
//     { auto f = DialogFooter(); if (Button("Save")) CloseDialog(); }
//   }
//
// Overlays open with shadcn's enter animation (fade + zoom from 95%), close on
// Escape and on a click outside, and keep keyboard/gamepad navigation inside.
#include <optional>
#include <span>
#include "silta/icons.hpp"
#include "silta/layout.hpp"

namespace silta {

enum class Side { top, bottom, left, right };

// ---------------------------------------------------------------- Popover
struct PopoverOptions {
  float width = 288;   // points (w-72); 0 = fit content
  float min_width = 0; // points, with width = 0
  Side side = Side::bottom;
  Align align = Align::center;
  float offset = 4;    // points between anchor and popover
  float padding = 16;  // points (p-4)
  float gap = 0;       // points between children
};

// Opens the popover `id`, anchored to the last item (call right after the
// trigger). Clicking the trigger again closes it.
void OpenPopover(const char *id);
// Closes the innermost open popover, menu or dialog (call from inside it).
void ClosePopover();
bool IsPopoverOpen(const char *id);

class [[nodiscard]] Popover {
public:
  Popover(const char *id, const PopoverOptions &options = {});
  ~Popover();
  Popover(const Popover &) = delete;
  Popover &operator=(const Popover &) = delete;
  explicit operator bool() const { return open_; }

  struct State;

private:
  bool open_ = false;
  State *state_ = nullptr;
};

// ---------------------------------------------------------- DropdownMenu
struct MenuOptions {
  float min_width = 128; // points (min-w-[8rem])
  Side side = Side::bottom;
  Align align = Align::start;
};

class [[nodiscard]] DropdownMenu {
public:
  DropdownMenu(const char *id, const MenuOptions &options = {});
  explicit operator bool() const { return static_cast<bool>(popover_); }

private:
  Popover popover_;
};

struct MenuItemOptions {
  Icon icon = Icon::none;
  const char *shortcut = nullptr; // e.g. "Ctrl+S", drawn muted on the right
  bool disabled = false;
  bool destructive = false;
  const bool *checked = nullptr;  // shows a check column when set
};

// True when chosen; the menu closes.
bool MenuItem(const char *label, const MenuItemOptions &options = {});
// Toggles *value; the menu stays open, like Radix's checkbox items.
bool MenuCheckbox(const char *label, bool *value);
void MenuLabel(const char *text);
void MenuSeparator();

// ---------------------------------------------------------------- Select
struct SelectOptions {
  const char *placeholder = "Select...";
  float width = 0; // points; 0 = widest item
  bool disabled = false;
  bool small = false; // h-8 instead of h-9
};

// A trigger that shows the chosen item and opens the list. *selected is an
// index into `items`, or -1 for none. Returns true when it changed.
bool Select(const char *id, int *selected, std::span<const char *const> items, const SelectOptions &options = {});

// ---------------------------------------------------------------- Tooltip
struct TooltipOptions {
  Side side = Side::top;
  float delay = 0.4f; // seconds of hover before it shows
};

// Shows `text` while the last item is hovered (or focused with the keyboard).
void Tooltip(const char *text, const TooltipOptions &options = {});

// ---------------------------------------------------------------- Dialog
struct DialogOptions {
  const char *title = nullptr;
  const char *description = nullptr;
  float width = 512;        // points (sm:max-w-lg)
  bool close_button = true; // the X in the corner
  bool dismissable = true;  // Escape and a click outside close it
};

void OpenDialog(const char *id);
inline void CloseDialog() { ClosePopover(); }

class [[nodiscard]] Dialog {
public:
  Dialog(const char *id, const DialogOptions &options = {});
  ~Dialog();
  Dialog(const Dialog &) = delete;
  Dialog &operator=(const Dialog &) = delete;
  explicit operator bool() const { return open_; }

  struct State;

private:
  bool open_ = false;
  State *state_ = nullptr;
};

// Buttons at the end of a dialog, right-aligned (sm:justify-end gap-2).
[[nodiscard]] inline Flex DialogFooter() {
  return Flex({.axis = Axis::row, .gap = 8, .align = Align::center, .justify = Justify::end, .width = fill});
}

} // namespace silta
