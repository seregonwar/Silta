#pragma once
// Flex layout for immediate mode.
//
//   { auto row = silta::Row({.gap = 8, .align = silta::Align::center});
//     silta::Button("Cancel", {.variant = silta::Variant::outline});
//     silta::Button("Deploy"); }
//
// Containers remember their children's sizes from the previous frame, so
// alignment, justification and grow work in one pass. When a size changes the
// layout asks for one more frame, which settles it before anyone sees it.
#include "imgui.h"
#include "imgui_internal.h"

namespace silta {

enum class Axis { row, column };
enum class Align { start, center, end, stretch };
enum class Justify { start, center, end, between };

struct Edges {
  float top = 0, right = 0, bottom = 0, left = 0;
  static constexpr Edges all(float v) { return {v, v, v, v}; }
  static constexpr Edges xy(float x, float y) { return {y, x, y, x}; }
  static constexpr Edges x(float v) { return {0, v, 0, v}; }
  static constexpr Edges y(float v) { return {v, 0, v, 0}; }
};

// Width or height that takes all available space (CSS `w-full`, or `flex-1`
// on a row's main axis). 0 means "fit content".
inline constexpr float fill = -1.f;

struct FlexOptions {
  Axis axis = Axis::column;
  float gap = 0;   // points
  Edges padding;   // points
  Align align = Align::start;
  Justify justify = Justify::start;
  float width = 0;  // points, 0 = fit content, fill = available
  float height = 0; // points, 0 = fit content, fill = available
  const char *id = nullptr; // stable identity; default: position in parent
};

namespace layout {
// Space a component of this size should occupy, in pixels. `grow` marks it as
// flexible on the parent's main axis.
ImRect allocate(ImVec2 size, bool grow = false);
// Pixel width a `fill` width resolves to here.
float fill_width();
// Pixel height a `fill` height resolves to here.
float fill_height();
// Resolves a points width (0 fit, fill, or fixed) given the content width.
float resolve_width(float points, float content_px);
float resolve_height(float points, float content_px);

// Tailwind breakpoints on the viewport width (sm 640, md 768, lg 1024, xl 1280).
enum class Breakpoint { sm, md, lg, xl };
bool at_least(Breakpoint breakpoint);

// Internal.
void begin_frame();
void end_frame();
bool settling();
} // namespace layout

class [[nodiscard]] Flex {
public:
  explicit Flex(const FlexOptions &options);
  ~Flex();
  Flex(const Flex &) = delete;
  Flex &operator=(const Flex &) = delete;
  explicit operator bool() const { return true; }
  // Outer rectangle in pixels (sizes that fit content come from last frame).
  ImRect rect() const;
  ImGuiID id() const { return id_; }

private:
  ImGuiID id_ = 0;
};

[[nodiscard]] inline Flex Row(FlexOptions options = {}) {
  options.axis = Axis::row;
  return Flex(options);
}
[[nodiscard]] inline Flex Column(FlexOptions options = {}) {
  options.axis = Axis::column;
  return Flex(options);
}

// Empty space (points) on the current main axis.
void Spacer(float points);

} // namespace silta
