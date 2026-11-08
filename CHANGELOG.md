# Changelog

Silta follows semantic versioning; before 1.0, a minor release may change the
API, and these notes say how to update.

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
