# Roadmap

Where Silta goes from 0.1 to a 1.0 that commercial products can ship on and
that other projects can adopt without friction.

Two questions decide the order:

1. **Can a product be sold with it?** Text in every language, accessibility,
   the platforms customers use, and no surprises in release builds.
2. **Can a project adopt it?** It must drop into an existing app or ImGui setup
   without breaking it, install like any C++ library, and be documented well
   enough to learn without reading the source.

Sizes are rough: **S** days, **M** one to two weeks, **L** several weeks.

## Starting point (0.1)

- Core library (theme, layout, animation, text, drawing, textures, components)
  builds on Windows, Linux and macOS in CI; the application shell runs on
  Windows only (Direct3D 11 and Vulkan, switchable at runtime).
- Components: Button, Badge, Card, Input, Switch, Checkbox, Slider, Tabs,
  Toggle/ToggleGroup, Progress, Kbd, Separator, Text/Label, Popover,
  DropdownMenu, Select, Tooltip, Dialog, Toast, ScrollArea, Split, Image,
  ~50 icons.
- Render on demand, GPU read-back screenshots, scripted input in hidden
  windows (gallery `--script`), MIT license, third-party notices, nightly
  packages, Silta Cut as a real-world example.

Known gaps this roadmap closes: global state and a global ImGui style reset,
no `install()`/`find_package`, Latin-only text, no accessibility, Windows-only
shell, no visual regression tests, limited component set, no docs site.

---

## 0.2: Embeddable (done)

Silta must be safe to add to an app that already uses Dear ImGui, and to
consume like any CMake package. Cheapest milestone, biggest effect on
adoption, so it comes first.

- [x] **Scoped styling (S).** `silta::init` stops resetting the global
      `ImGuiStyle`. Silta components draw with Silta's theme; ImGui's own
      widgets keep the host's style. Opt-in helper to theme ImGui widgets too.
- [x] **Bring your own ImGui (S).** `SILTA_IMGUI_TARGET` to use the host's
      ImGui target instead of fetching one; a configure-time check of the
      minimum version (1.92.8) and of required defines.
- [x] **State per context (M).** Move the global maps (layout memory, tweens,
      overlay memory, toasts, tooltip state, texture graveyard) into a
      `silta::Context` attached to each ImGui context. Several windows and
      several ImGui contexts (e.g. a projector window) work side by side.
- [x] **Install and export (S).** `install()` rules, `SiltaConfig.cmake`,
      `find_package(Silta 0.2 CONFIG)`, targets `Silta::silta` and
      `Silta::app`; works with CPM and `add_subdirectory` too.
- [x] **Fonts without files (S).** Inter embedded (compressed) by default,
      so a consumer does not have to ship a `fonts/` folder; custom fonts
      still loadable from files or memory.
- [x] **Embedding guide (S).** `docs/embedding.md`: Silta inside an engine
      or player that owns the renderer (init, per-frame calls, textures,
      render-on-demand hooks), with a minimal example under `examples/`.

**Done when** an existing ImGui app adds Silta with `find_package`, draws a
Silta card next to its own ImGui windows, and neither changes the other's
look; CI builds an out-of-tree consumer project against the installed package.

## 0.3: Text for every market

Products are sold in more than Latin-script markets.

- [x] **Shaping (L).** HarfBuzz for complex scripts (Arabic, Hebrew,
      Devanagari, Thai) and emoji sequences; shaped lines cached by string,
      weight and size so immediate mode stays cheap. Text that does not need
      it keeps ImGui's atlas path.
- [x] **Bidirectional text (M).** Right-to-left and mixed runs ordered with
      the Unicode bidi algorithm (SheenBidi).
- [x] **RTL layout (S).** Mirrored layout direction for rows and components:
      `set_direction(Direction::rtl)` mirrors rows, alignment, component
      content, sliders, menus and text alignment.
- [x] **Shaped editing (M).** Input and Textarea edit right-to-left and mixed
      text through the shaper: joined glyphs, right-anchored lines, and a
      caret, selection, delete and clipboard shortcuts that follow visual
      order. Plain left-to-right fields keep ImGui's editor.
- [x] **Font fallback (M).** Per-codepoint fallback chain: Inter, then
      system fonts found per platform (CJK ordered for the user's language,
      Indic, Thai, symbols), colour emoji; files memory-mapped.
- [x] **Textarea (M).** Multi-line editing: wrapping, selection, undo,
      clipboard, scrolling, placeholder; grows with its content.
- [ ] **IME (S).** Verify and fix composition (Japanese, Chinese, Korean)
      in Input and Textarea on each platform shell; candidate window placed
      at the caret.
- [ ] **Selectable text (S).** Opt-in selection and copy for `Text`.

**Done when** the gallery has a language switcher showing Arabic, Hebrew,
Hindi, Chinese, Japanese, Korean and emoji correctly, and visual regression
images cover each.

## 0.4: Platforms

- [ ] **macOS shell (L).** Cocoa window + Metal renderer (ImGui's Metal
      backend), Retina scale, native menus hook, `NSOpenPanel`/`NSSavePanel`.
- [ ] **Linux shell (M).** GLFW on Wayland and X11 with Vulkan; file dialogs
      through xdg-desktop-portal.
- [ ] **Fallback renderers (M).** OpenGL 3.3 (and Direct3D 12 on Windows),
      chosen automatically when Vulkan or D3D11 fail.
- [ ] **Device loss (S).** Recreate the renderer after a GPU reset or driver
      update without losing UI state (textures are re-uploaded by ImGui).
- [ ] **Window chrome (M).** Optional custom title bar drawn by Silta with
      native behaviour (drag, snap, double-click maximise).

**Done when** the gallery and Silta Cut run on Windows, macOS and Linux from
the same sources, CI builds all three shells, and visual regression runs on
each.

## 0.5: Mobile

Phones and tablets are first-class targets for 1.0: the same Silta code runs
on Android and iOS/iPadOS, with touch as the primary input.

- [ ] **Android shell (L).** GameActivity (or NativeActivity) host, Vulkan
      with an OpenGL ES 3 fallback, surface loss and recreation across
      pause/resume, safe-area insets (cutouts, gesture bar), system back
      gesture, density changes and multi-window.
- [ ] **iOS and iPadOS shell (L).** UIKit view with Metal, scene lifecycle,
      safe areas, keyboard avoidance, pointer and Apple Pencil input, Split
      View and Stage Manager sizes.
- [ ] **Touch input (M).** Tap, long press, pan, pinch and fling recognised
      once and shared by all components; momentum scrolling with overscroll;
      pressed states instead of hover; minimum 44-point touch targets.
- [ ] **Soft keyboard and IME (M).** Show/hide the system keyboard for text
      fields, keyboard types (numbers, email, URL), composition (CJK) and
      autocorrect handled by the platform, the focused field kept visible.
- [ ] **Touch-adapted components (M).** Bottom `Sheet`, menus and `Select`
      as sheets on phones, tooltips on long press, larger slider thumbs,
      responsive defaults by breakpoint, swipe actions for list rows.
- [ ] **Platform services (S).** File pickers (Storage Access Framework,
      `UIDocumentPickerViewController`), share sheet, clipboard, haptics,
      keep-awake.
- [ ] **Battery and memory (S).** Render on demand already applies; add
      background suspension, texture memory budgets and low-memory warnings.
- [ ] **Project templates (M).** Gradle/AGP template and CMake-generated
      Xcode project, signing left to the app; CI builds an APK and an iOS
      simulator app.

**Done when** the gallery and a sample app run on an Android phone and tablet
and on iPhone and iPad from the same sources, CI builds both, and the visual
regression and interaction scripts run on an Android emulator and an iOS
simulator.

## 0.6: Accessibility

The European Accessibility Act applies to many consumer products sold in the
EU since June 2025; screen reader support is also a requirement for many
enterprise buyers.

- [ ] **AccessKit integration (L).** Each frame Silta builds an accessibility
      tree from its components (stable IDs already exist) and hands updates to
      AccessKit's C API; adapters for Windows (UIA), macOS (NSAccessibility)
      and Linux (AT-SPI), and the platform accessibility services on Android
      (TalkBack) and iOS (VoiceOver).
- [ ] **Roles, names and states (M)** for every component: button, checkbox,
      switch, slider (value and range), tabs, menu and menu items, list box
      (Select), dialog (modal), tooltip (description), text field.
- [ ] **Focus management (M).** Focus trap inside dialogs, focus returns to
      the trigger when an overlay closes, visible focus everywhere, logical
      tab order across layouts.
- [ ] **Preferences (S).** Respect reduced motion (skip animations), high
      contrast, and system text scaling.

**Done when** Narrator, VoiceOver (macOS and iOS), Orca and TalkBack can
operate the gallery and the Silta Cut main screens (desktop: with keyboard
only); an automated test walks the
accessibility tree of the gallery.

## 0.7: Components and documentation

Can run in parallel with 0.3 to 0.6.

- [ ] **Data (L).** `DataTable` (sorting, column resize, selection,
      virtualized rows for 100k items), virtualized `List`, `Tree`.
- [ ] **Input (M).** `Combobox` (searchable select), `Command` (command
      palette), `RadioGroup`, `Calendar` and `DatePicker`, `InputOTP`,
      `NumberInput`, form validation helpers (error text, `aria-invalid`
      style).
- [ ] **Navigation and structure (M).** `Sidebar`, `Sheet`/`Drawer`,
      `Accordion`/`Collapsible`, `ContextMenu`, `Menubar`, `Breadcrumb`,
      `Pagination`, `NavigationMenu`.
- [ ] **Feedback (S).** `Alert`, `AlertDialog`, `Skeleton`, `Avatar`,
      `HoverCard`, `Spinner`.
- [ ] **Motion (M).** Exit animations for overlays and toasts, smooth and
      kinetic scrolling, list reordering by drag and drop.
- [ ] **Charts (M).** Line, bar, area and pie, styled from theme tokens.
- [ ] **Theming (M).** Load shadcn themes at runtime (CSS variables or
      tweakcn JSON); configurable fonts, type scale and spacing, not only
      colours.
- [ ] **Docs site (L).** Emscripten + WebGPU build of the gallery so every
      component has a live demo and its code in the browser, like
      ui.shadcn.com; API reference generated from headers; guides (getting
      started, layout, theming, embedding, textures, testing).
- [ ] **Blocks (M).** Ready-made screens: login, settings, dashboard, editor
      layout, empty states.

**Done when** the docs site is public with a live demo for every component,
and the shadcn component list is covered except web-only pieces.

## 0.8: Bindings

- [ ] **C API (L).** A stable C ABI over the public API (POD option structs,
      opaque handles for textures and scopes), versioned separately.
- [ ] **Rust crate (M)** on the C API, with RAII scopes and builder options.
- [ ] **C# and Python (M)** generated from the same C header.

**Done when** the gallery's main sections are reproduced in Rust and C#
examples that build in CI.

## 1.0: Stable

- [ ] API review and freeze; semantic versioning from here on, deprecations
      announced one minor release before removal.
- [ ] Shared and static builds, ABI policy documented (C API stable, C++ API
      source-stable).
- [ ] Packages in vcpkg and Conan, release archives with notices.
- [ ] Performance budgets enforced in CI (see below).
- [ ] Security and robustness pass: fuzzing of text input and file paths,
      no reliance on asserts in release builds.

---

## Cross-cutting work (every milestone)

**Testing.**
- Visual regression: the hidden-window `--script` harness renders each gallery
  section in light and dark mode on every backend; CI compares against
  reference PNGs with a small tolerance and uploads diffs.
- Unit tests for layout, animation, colour conversion and text measurement.
- Interaction tests (click, keyboard, overlays) through the same script
  player, promoted from the gallery into a reusable `silta::testing` module.

**Performance budgets** (measured in CI on a reference machine):
- Idle: zero frames rendered, ~0% CPU.
- Gallery frame: under 2 ms CPU on the UI thread in release builds.
- Memory: under 60 MB for the gallery, fonts included.
- Startup to first frame: under 300 ms.

**Release process.** Changelog per release, migration notes for breaking
changes before 1.0, signed tags, packaged notices (THIRD_PARTY.md) checked in
CI.

**Licensing hygiene.** Every new dependency must allow closed-source,
statically linked commercial use (MIT, BSD, zlib, Apache-2.0, FTL, OFL for
fonts). LGPL stays limited to optional, dynamically linked parts (FFmpeg in
Silta Cut).

## Not planned before 1.0

- **A retained-mode API.** Silta stays immediate mode; the per-frame tree it
  builds for layout and accessibility is internal.
- **Copy-paste distribution** in the style of shadcn's CLI. Silta is a
  versioned library; blocks and examples cover the "start from this" need.
