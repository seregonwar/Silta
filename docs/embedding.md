# Embedding Silta

Silta does not need its own window. An application that already owns a window,
a renderer and a Dear ImGui setup (a game engine, a video player, a tool built
on ImGui) adds Silta with a few calls and keeps everything it has.

The [embed example](../examples/embed/main.cpp) is a complete Win32 + Direct3D
11 host with ImGui's own dark style and a Silta card in one of its windows.

## Linking

Link `Silta::silta` (not `Silta::app`, which is the optional window shell):

```cmake
find_package(Silta 0.2 CONFIG REQUIRED)
target_link_libraries(host PRIVATE Silta::silta)
```

By default Silta fetches and builds Dear ImGui 1.92.9b and exposes it through
`Silta::silta`. A host with its own ImGui builds Silta against it instead:

```cmake
# host_imgui: the host's ImGui target, version 1.92.8 or newer
set(SILTA_IMGUI_TARGET host_imgui CACHE STRING "" FORCE)
set(SILTA_APP OFF CACHE BOOL "" FORCE)        # no window shell needed
add_subdirectory(external/Silta)
target_link_libraries(host PRIVATE Silta::silta host_imgui)
```

Configuration fails with a clear message if the target's ImGui is older than
1.92.8. Silta uses whatever ImGui configuration the target has (FreeType or
stb_truetype, `imconfig.h` options). To install Silta built this way, the
ImGui target must be an imported one (for example from vcpkg's `imgui` port),
so the installed package can refer to it.

## Per-frame calls

```cpp
ImGui::CreateContext();
// ... the host's ImGui setup: style, fonts, platform and renderer backends ...
silta::init({.theme = silta::Theme::dark(), .scale = dpi_scale});

while (running) {
  // ... platform and renderer NewFrame ...
  ImGui::NewFrame();
  silta::begin_frame();

  ImGui::Begin("Inspector");          // a host window
  ImGui::Checkbox("ImGui widget", &v);   // keeps the host's style
  {
    auto card = silta::Card({.width = silta::fill});   // Silta's theme
    silta::Button("Export");
  }
  ImGui::End();
  silta::Toaster();                   // if the app uses toasts

  silta::end_frame();
  ImGui::Render();
  // ... render ImGui's draw data with the host's renderer ...
}

ImGui::DestroyContext();              // also frees Silta's state
```

Silta components can live in any ImGui window, popup or child; they lay
themselves out from the window's cursor. Call `silta::set_scale()` when the
window moves to a monitor with another DPI, and `silta::set_theme()` to switch
themes. For right-to-left languages pass `.direction = silta::Direction::rtl`
to `silta::init` or call `silta::set_direction()` at any time: rows and
components mirror, and text aligned with the layout starts at the right.

### Text fields and right-to-left editing

With `SILTA_SHAPING` on (the default), `Input` and `Textarea` in an RTL layout
draw and edit through Silta's shaper: Arabic and Hebrew join, lines start at
the right edge, and the caret, selection, arrow keys, Home/End, Backspace and
Delete work in visual order, including mixed runs such as Arabic with Latin
words or numbers. The value stays a normal UTF-8 `std::string`; byte offsets
are the storage model. A field in an LTR layout whose text needs no shaping
still uses ImGui's editor, so the common case keeps ImGui's behaviour and
performance. Password fields always use ImGui's editor.

Clipboard shortcuts (`Ctrl+A/C/X/V`) work on the visual selection: they copy
and cut in logical order, paste over the selected range, and leave the caret
after the inserted text. The shaped editor uses ImGui's clipboard functions,
so a headless host must provide
`ImGuiPlatformIO::Platform_GetClipboardTextFn` and `Platform_SetClipboardTextFn`
(the gallery's hidden runs install an in-memory pair for scripted tests).
Pasted CRLF is normalized to LF in a `Textarea`, and single-line fields drop
line breaks and turn tabs into spaces.

Caveats: IME composition (Japanese, Chinese, Korean) is not yet supported by
the shaped editor; an RTL field receives only the committed characters. Builds
with `SILTA_SHAPING=OFF` fall back to ImGui's editor for every field.

## What Silta changes, and what it does not

- **Style.** `silta::init` leaves `ImGuiStyle` alone. Silta's components draw
  with Silta's theme and push the few ImGui style values they need (for text
  fields, scroll areas, disabled items) only while they draw. To theme ImGui's
  own widgets and windows as well, pass `.style_imgui = true` or call
  `silta::style_imgui()`; `silta::App` does this for its windows.
- **Fonts.** Silta adds three Inter faces to the context's font atlas (built
  into the library, or from `InitOptions::regular/medium/semibold` as files or
  memory). If the host has not loaded any font, Silta adds ImGui's default font
  first, so ImGui's default stays ImGui's.
- **Modal dim.** Silta's `Dialog` draws its own overlay and clears ImGui's
  modal dim colour only for the frames in which a Silta dialog is open.
- **State.** Layout memory, animations, overlays, toasts and textures belong
  to the current ImGui context. Several contexts (several windows, a projector,
  an embedded editor) can each run Silta; `ImGui::DestroyContext` frees that
  context's Silta state.

## Textures

`silta::Texture` uploads CPU pixels (RGBA8) through ImGui 1.92's texture
lifecycle, so it works with any renderer backend that sets
`ImGuiBackendFlags_RendererHasTextures` (all of ImGui's 1.92 backends do).
Update it every frame for video. Textures created with your own renderer can
be drawn with `ImDrawList::AddImage` as usual, next to Silta's components.

## Rendering on demand

Silta knows when it needs another frame (animations, layout settling, timers
such as tooltip delays and toasts). A host that sleeps between inputs can ask:

```cpp
if (!silta::wants_frame() && !host_needs_frame) {
  const double timeout = silta::idle_timeout();   // < 0: wait for input
  wait_for_events(timeout);
}
```

A host that renders continuously (a game loop) can ignore these calls.

## Testing an integration

Silta draws through ImGui's draw lists only, so an integration can be tested
headlessly: create a context, set `ImGuiBackendFlags_RendererHasTextures`,
run frames and acknowledge texture requests, as
[tests/package/main.cpp](../tests/package/main.cpp) does.
