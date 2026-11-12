# Changelog

Silta follows semantic versioning; before 1.0, a minor release may change the
API, and these notes say how to update.

## Unreleased (0.3 in progress)

### Added
- Metal renderer backend (`Backend::metal`) for the macOS window shell, selected
  automatically on Apple and switchable at runtime with Vulkan/MoltenVK; GPU
  frame read-back works for screenshots and visual regression.
- iOS and iPadOS application shell: `silta/ios.hpp` runs a UIApplication (legacy
  window lifecycle) with a Metal renderer, touch and Apple Pencil input, the
  soft keyboard, safe-area-aware root window and the usual render-on-demand loop.
- Android application shell: `silta/android.hpp` runs a NativeActivity host
  (android_native_app_glue, NDK r23+) with a Vulkan renderer, touch and key
  input (with Unicode characters), soft-keyboard follow, surface
  recreation across pause/resume and content-rect insets.
- Renderers can now serve native surfaces: `init_native` hands a shell-owned
  MTKView (Metal) or ANativeWindow (Vulkan) to the shell-free render loop.
- CI: the macOS job builds the applications with Metal and renders a Gallery
  screenshot through it; new jobs build the Android shell with the NDK and the
  iOS shell with Xcode.
- Right-to-left layout: `InitOptions::direction` and `silta::set_direction()`
  mirror rows, cross-axis alignment, justification and component content
  (button and badge icons, checkbox and switch controls, tabs, slider,
  progress, menu rows, select and dialog chrome, toasts). `Text` and
  `TextOptions::align` follow the layout direction.
- Fallback fonts: characters Inter lacks come from system fonts (CJK in the
  user's language order, Indic, Thai, symbols, colour emoji). Configurable
  with `InitOptions::fallback` and `system_fallback`; font files are
  memory-mapped instead of copied.
- Text shaping: `Text`, `Label` and every component label shape complex
  scripts with HarfBuzz (Arabic joining, Indic conjuncts, Thai marks) and
  order right-to-left and mixed text with the Unicode bidi algorithm
  (SheenBidi). Emoji modifier, flag and ZWJ sequences become one glyph.
  Enabled by `SILTA_SHAPING` (default on); Latin, CJK and other simple text
  still uses ImGui's atlas.
- Shaped editing: `Input` and `Textarea` lay out and edit right-to-left and
  mixed text through the shaper, with joined Arabic/Hebrew glyphs,
  right-anchored fields and a caret, selection and delete that follow visual
  order. Copy, cut and paste (`Ctrl+A/C/X/V`) act on that same visual
  selection, and pasted text keeps its shaped order. Plain left-to-right
  fields keep ImGui's editor, and password fields always do.
- `Textarea`: multi-line field that wraps and grows with its content.
- ImGui is built with 32-bit characters (`IMGUI_USE_WCHAR32`) so emoji and
  other characters beyond U+FFFF render.
- Gallery: a Languages section (with Hebrew, mixed bidi text and emoji
  sequences) and a Textarea.

### Fixed
- A row of automatic height did not grow with its stretched children (the
  default since 0.2), so long pages could not scroll to their end.

## 0.2.0: Embeddable

Silta can be added to an application that already uses Dear ImGui without
changing it, and installed like any CMake package.

### Added
- `find_package(Silta 0.2 CONFIG)` with targets `Silta::silta` and
  `Silta::app` (install rules, `SiltaConfig.cmake`, version file).
- `SILTA_IMGUI_TARGET` to build against the host's ImGui (1.92.8+, checked at
  configure time) and `SILTA_IMGUI_BACKENDS_DIR` for the shell's backends.
- Inter fonts built into the library (`SILTA_EMBED_FONTS`, on by default).
- `InitOptions::regular/medium/semibold` (`FontSource`: a file or memory).
- `InitOptions::style_imgui` and `silta::style_imgui()` to theme ImGui's own
  widgets on request.
- `silta::version()`.
- State per ImGui context: several contexts can run Silta side by side.
- `examples/embed` (Win32 + Direct3D 11 host) and `docs/embedding.md`.
- Tests: two contexts with separate state, an installed-package consumer, and
  a host project with its own ImGui target.

### Changed
- `silta::init` no longer resets `ImGuiStyle` or the default font. Components
  push the style they need while they draw. Apps built on `silta::App` look
  the same as before (the shell turns `style_imgui` on).
- `silta::shutdown()` is optional: `ImGui::DestroyContext` frees Silta's state.
- Silta's `Dialog` draws its own overlay instead of relying on ImGui's modal
  dim colour.
- `Input` with `std::string` no longer needs ImGui's `misc/cpp` sources.

### Migration from 0.1
- An embedding host that relied on Silta theming its ImGui widgets passes
  `.style_imgui = true` to `silta::init` (or calls `silta::style_imgui()`).
- `InitOptions::font_dir` still works; it is only needed with
  `-DSILTA_EMBED_FONTS=OFF` or to load other Inter files.
- Executables no longer need a `fonts/` folder next to them.

## 0.1.0

First release: theme tokens, flex layout, animations, typography, components
(buttons, forms, overlays, panels, toasts), textures, the application shell
with Direct3D 11 and Vulkan, the Gallery and Silta Cut.
