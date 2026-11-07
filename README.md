# Silta

A native C++ design system with the look and API of [shadcn/ui](https://ui.shadcn.com),
built on Dear ImGui with interchangeable renderer backends.

- **shadcn tokens, exactly**: `background`, `primary`, `muted`, `ring`, `radius`…
  defined in OKLCH like Tailwind v4 and converted at startup. All five base colors
  (neutral, zinc, slate, stone, gray), light and dark.
- **CSS semantics**: Tailwind's type scale and line heights, `box-shadow`,
  inside borders, focus rings (`ring-[3px] ring-ring/50`), `opacity-50` when disabled,
  150 ms `cubic-bezier(0.4, 0, 0.2, 1)` transitions.
- **Flex layout** for immediate mode: `Row`/`Column` with gap, padding, align,
  justify, `fill` (w-full / flex-1) and Tailwind breakpoints.
- **Renderer agnostic**: components draw only through `ImDrawList`, so they look the
  same on every backend. The app shell ships Direct3D 11 and Vulkan, switchable at
  runtime.
- **Renders on demand**: no frames while nothing moves (input, animations and layout
  changes wake it up).

Dear ImGui is the infrastructure (input, IDs, draw lists, backends). Silta does not
use ImGui's own widgets, except `InputText` for text editing inside `Input`.

## Example

```cpp
#include "silta/app.hpp"
#include "silta/silta.hpp"
using namespace silta;

int main() {
  App app({.title = "Hello"});
  std::string name;
  return app.run([&] {
    auto page = Column({.gap = 16, .padding = Edges::all(32)});
    if (auto card = Card({.width = 350})) {
      { auto h = CardHeader();
        CardTitle("Create project");
        CardDescription("Deploy your new project in one-click."); }
      { auto c = CardContent();
        Input("name", &name, {.placeholder = "Name of your project"}); }
      { auto f = CardFooter({.justify = Justify::between});
        Button("Cancel", {.variant = Variant::outline});
        if (Button("Deploy")) { /* ... */ } }
    }
  });
}
```

Apps that already own a renderer (a game engine, a video player) skip `silta::App`:
they call `silta::init()` once and `silta::begin_frame()` / `end_frame()` inside
their own ImGui frame.

## Components

| Component | Options |
|---|---|
| `Button` | variants `primary`, `secondary`, `destructive`, `outline`, `ghost`, `link`; sizes `sm`, `md`, `lg`, `icon`; leading/trailing icon; `loading`; `disabled`; `width` |
| `Badge` | `primary`, `secondary`, `destructive`, `outline`; icon |
| `Card` | `CardHeader`, `CardTitle`, `CardDescription`, `CardContent`, `CardFooter` |
| `Switch` | label (clickable), `disabled` |
| `Input` | `placeholder`, `password`, `disabled`, `width`; `char*` or `std::string` |
| `Separator` | horizontal or vertical |
| `Text`, `Label` | Tailwind sizes `xs`…`4xl`, weights, muted, wrapping, `leading-*` |
| Icons | Lucide-style vector strokes (check, x, plus, chevrons, mail, search, loader, bell, settings, user) |

## Build

Requirements: CMake 3.24+, a C++20 compiler; the Vulkan SDK for the Vulkan backend.
Dependencies (Dear ImGui 1.92.9b, FreeType 2.13.3, GLFW 3.4) are fetched and pinned
by CMake.

```bash
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --target silta_gallery
build/examples/gallery/Release/silta_gallery.exe
```

CMake options: `SILTA_FREETYPE` (on), `SILTA_APP` (on), `SILTA_RENDERER_D3D11`
(on for Windows), `SILTA_RENDERER_VULKAN` (on), `SILTA_EXAMPLES`.

## Gallery

`examples/gallery` shows every component in every variant, with live theme, base
color and renderer switching. Options:

| Option | Effect |
|---|---|
| `--backend=d3d11` / `--backend=vulkan` | renderer to start with |
| `--dark`, `--base=zinc` (…) | initial theme |
| `--switch-to=vulkan` | swap the renderer at frame 3 (tests runtime switching) |
| `--hidden` | never show the window |
| `--script="click:978,56;type:hi;key:tab;wait:10;shot:out.png;quit"` | play input (points from the window's top-left) and save GPU read-back screenshots |

`SILTA_TRACE=<file>` logs why each frame was rendered.

## Layout

```
include/silta/   public API (theme, layout, anim, text, draw, icons, components, app)
src/             core and components (renderer agnostic)
src/app/         window + renderer backends (GLFW, D3D11, Vulkan), PNG screenshots
examples/        gallery
assets/fonts/    Inter 4.1 (SIL Open Font License, see Inter-LICENSE.txt)
```

## Status

Version 0.1, Windows-tested (D3D11 and Vulkan). Not done yet: Metal and other
platforms' shells, more components (Checkbox, Select, Dialog, Popover, Tabs,
Tooltip…), accessibility, a Rust binding over a C ABI.
