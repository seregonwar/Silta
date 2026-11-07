#pragma once
// Regions: ScrollArea (clipped, scrollable) and Split (resizable panes).
//
//   auto split = Split("main", {.initial = 0.25f});
//   { auto left = split.first(); ... }
//   { auto right = split.second(); ... }
#include <optional>
#include "silta/layout.hpp"

namespace silta {

struct ScrollAreaOptions {
  float width = fill;  // points or fill
  float height = fill; // points or fill
  Edges padding;
  float gap = 0;
  bool horizontal = false; // also scroll sideways
};

class [[nodiscard]] ScrollArea {
public:
  ScrollArea(const char *id, const ScrollAreaOptions &options = {});
  ~ScrollArea();
  ScrollArea(const ScrollArea &) = delete;
  ScrollArea &operator=(const ScrollArea &) = delete;
  explicit operator bool() const { return true; }

private:
  ImVec2 saved_cursor_;
  std::optional<layout::Isolate> isolate_;
  std::optional<Flex> content_;
};

struct SplitOptions {
  Axis axis = Axis::row;    // row: panes side by side; column: stacked
  float initial = 0.3f;     // first pane's share before the user drags
  float min_first = 120;    // points
  float min_second = 120;   // points
  float width = fill;
  float height = fill;
};

class [[nodiscard]] Split {
public:
  Split(const char *id, const SplitOptions &options = {});
  // The panes, in order. Each is a Flex filling its pane (column inside).
  [[nodiscard]] Flex first(FlexOptions options = {});
  [[nodiscard]] Flex second(FlexOptions options = {});
  explicit operator bool() const { return true; }

private:
  SplitOptions options_;
  ImGuiID id_ = 0;
  float first_px_ = 0;
  Flex outer_;
};

} // namespace silta
