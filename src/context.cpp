#include "silta/context.hpp"
#include "silta/anim.hpp"
#include "silta/layout.hpp"
#include "silta/texture.hpp"
#include "fonts.hpp"
#include "per_context.hpp"
#include "imgui.h"
#ifdef IMGUI_ENABLE_FREETYPE
#include "misc/freetype/imgui_freetype.h"
#endif
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

#ifdef SILTA_EMBED_FONTS
// Generated at build time from assets/fonts (see cmake/EmbedFonts.cmake).
namespace silta::detail {
extern const unsigned char inter_regular[], inter_medium[], inter_semibold[];
extern const std::size_t inter_regular_size, inter_medium_size, inter_semibold_size;
} // namespace silta::detail
#endif

namespace silta {
namespace {
struct State {
  Theme theme = Theme::light();
  float scale = 1.f;
  Fonts fonts;
  int pending_frames = 2;
  bool style_imgui = false;
  Direction direction = Direction::ltr;
  double wake_at = -1;           // ImGui time of the earliest requested wake-up
  std::vector<std::unique_ptr<detail::MappedFile>> font_files; // mapped font files
  detail::FaceData inter[3];            // regular, medium, semibold
  std::vector<detail::FaceData> fallback; // in lookup order
};
State &S() { return detail::per_context<State>(); }

std::filesystem::path executable_dir() {
#ifdef _WIN32
  wchar_t buf[MAX_PATH];
  const DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
  if (n > 0 && n < MAX_PATH) return std::filesystem::path(buf).parent_path();
#elif defined(__APPLE__)
  char buf[4096];
  uint32_t size = sizeof buf;
  if (_NSGetExecutablePath(buf, &size) == 0) return std::filesystem::path(buf).parent_path();
#else
  std::error_code ec;
  auto p = std::filesystem::read_symlink("/proc/self/exe", ec);
  if (!ec) return p.parent_path();
#endif
  return std::filesystem::current_path();
}

uint16_t be16(const unsigned char *p) { return uint16_t((p[0] << 8) | p[1]); }
uint32_t be32(const unsigned char *p) { return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3]; }

// (hhea.ascender - hhea.descender) / head.unitsPerEm: how much larger ImGui's
// size unit is than CSS's em for this font.
float em_ratio(const unsigned char *d, std::size_t size) {
  if (!d || size < 12) return 1.f;
  const uint16_t tables = be16(&d[4]);
  int asc = 0, desc = 0, upm = 0;
  for (uint16_t i = 0; i < tables; ++i) {
    const size_t rec = 12 + size_t(i) * 16;
    if (rec + 16 > size) break;
    const uint32_t off = be32(&d[rec + 8]);
    if (std::memcmp(&d[rec], "hhea", 4) == 0 && off + 8 <= size) {
      asc = int16_t(be16(&d[off + 4]));
      desc = int16_t(be16(&d[off + 6]));
    } else if (std::memcmp(&d[rec], "head", 4) == 0 && off + 20 <= size) {
      upm = be16(&d[off + 18]);
    }
  }
  return upm > 0 && asc - desc > 0 ? float(asc - desc) / float(upm) : 1.f;
}

std::filesystem::path from_u8(const char *s) {
  return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t *>(s)));
}

struct Bytes {
  const unsigned char *data = nullptr;
  std::size_t size = 0;
};
// Font bytes from memory, or from a file mapped for the context's lifetime.
Bytes read_source(const char *path, const void *data, std::size_t size) {
  if (data) return {static_cast<const unsigned char *>(data), size};
  if (!path) return {};
  auto file = detail::MappedFile::open(path);
  if (!file) return {};
  const Bytes bytes{file->data(), file->size()};
  S().font_files.push_back(std::move(file));
  return bytes;
}
Bytes read_source(const FontSource &src) { return read_source(src.path, src.data, src.size); }

// A face in the atlas; with `merge`, it adds the glyphs the previous font
// lacks (ImGui looks glyphs up source by source, in order).
ImFont *add_font(Bytes bytes, const char *name, int index = 0, bool merge = false, bool color = false) {
  if (!bytes.data) return nullptr;
  ImFontConfig cfg;
#ifdef IMGUI_ENABLE_FREETYPE
  cfg.FontLoaderFlags = ImGuiFreeTypeLoaderFlags_LightHinting;
  if (color) cfg.FontLoaderFlags |= ImGuiFreeTypeLoaderFlags_LoadColor;
#else
  (void)color;
#endif
  cfg.PixelSnapH = true;
  cfg.MergeMode = merge;
  cfg.FontNo = index;
  cfg.FontDataOwnedByAtlas = false; // Silta (or the caller) owns the bytes
  std::snprintf(cfg.Name, sizeof cfg.Name, "%s", name);
  return ImGui::GetIO().Fonts->AddFontFromMemoryTTF(const_cast<unsigned char *>(bytes.data), int(bytes.size), 0.f,
                                                     &cfg);
}

struct Fallback {
  Bytes bytes;
  int index = 0;
  bool color = false;
};

// Explicit fallbacks first, then the system's.
std::vector<Fallback> fallback_fonts(const InitOptions &o) {
  std::vector<Fallback> out;
  for (const FontSource &src : o.fallback)
    if (Bytes b = read_source(src); b.data) out.push_back({b, src.index, false});
  if (o.system_fallback)
    for (const detail::SystemFont &f : detail::system_fallback_fonts())
      if (Bytes b = read_source(f.path.c_str(), nullptr, 0); b.data) out.push_back({b, f.index, f.color});
  return out;
}

ImFont *add_face(Bytes bytes, int index, const char *name, const std::vector<Fallback> &fallbacks) {
  ImFont *font = add_font(bytes, name, index);
  if (font)
    for (const Fallback &f : fallbacks) add_font(f.bytes, "fallback", f.index, true, f.color);
  return font;
}

bool load_fonts(const InitOptions &o) {
  State &st = S();
  // 1. Explicit sources.
  FontSource regular = o.regular, medium = o.medium, semibold = o.semibold;
  // 2. The faces built into the library.
#ifdef SILTA_EMBED_FONTS
  if (regular.empty() && !o.font_dir) {
    regular = {nullptr, detail::inter_regular, detail::inter_regular_size};
    if (medium.empty()) medium = {nullptr, detail::inter_medium, detail::inter_medium_size};
    if (semibold.empty()) semibold = {nullptr, detail::inter_semibold, detail::inter_semibold_size};
  }
#endif
  // 3. Inter files in a folder.
  std::vector<std::string> dir_paths; // keeps path strings alive
  if (regular.empty()) {
    std::vector<std::filesystem::path> dirs;
    if (o.font_dir) dirs.push_back(from_u8(o.font_dir));
    dirs.push_back(executable_dir() / "fonts");
#ifdef SILTA_DEFAULT_FONT_DIR
    dirs.emplace_back(SILTA_DEFAULT_FONT_DIR);
#endif
    for (const auto &dir : dirs) {
      std::error_code ec;
      if (!std::filesystem::exists(dir / "Inter-Regular.ttf", ec)) continue;
      for (const char *file : {"Inter-Regular.ttf", "Inter-Medium.ttf", "Inter-SemiBold.ttf"}) {
        const std::u8string u8 = (dir / file).u8string();
        dir_paths.emplace_back(u8.begin(), u8.end());
      }
      regular.path = dir_paths[0].c_str();
      if (medium.empty()) medium.path = dir_paths[1].c_str();
      if (semibold.empty()) semibold.path = dir_paths[2].c_str();
      break;
    }
  }

  // ImGui uses the atlas' first font as its default. A host that has not
  // loaded one keeps ImGui's own default rather than silently getting Inter.
  ImFontAtlas *atlas = ImGui::GetIO().Fonts;
  if (!o.style_imgui && atlas->Fonts.empty()) atlas->AddFontDefault();

  const Bytes r = read_source(regular);
  st.fonts = {};
  if (!r.data) {
    std::fprintf(stderr, "silta: Inter fonts not found, falling back to ImGui's default font\n");
    st.fonts.em_to_imgui = 1.f;
    return false;
  }
  // Each weight gets the same fallback chain; the files are mapped once.
  const std::vector<Fallback> fallbacks = fallback_fonts(o);
  st.fonts.fallback_count = int(fallbacks.size());
  const Bytes m = read_source(medium), sb = read_source(semibold);
  st.fonts.regular = add_face(r, regular.index, "Inter Regular", fallbacks);
  st.fonts.medium = add_face(m, medium.index, "Inter Medium", fallbacks);
  st.fonts.semibold = add_face(sb, semibold.index, "Inter SemiBold", fallbacks);
  // The same faces, for the text shaper (missing weights use the previous).
  st.inter[0] = {r.data, r.size, regular.index, false};
  st.inter[1] = m.data ? detail::FaceData{m.data, m.size, medium.index, false} : st.inter[0];
  st.inter[2] = sb.data ? detail::FaceData{sb.data, sb.size, semibold.index, false} : st.inter[1];
  st.fallback.clear();
  for (const Fallback &f : fallbacks) st.fallback.push_back({f.bytes.data, f.bytes.size, f.index, f.color});
  if (!st.fonts.medium) st.fonts.medium = st.fonts.regular;
  if (!st.fonts.semibold) st.fonts.semibold = st.fonts.medium;
  st.fonts.em_to_imgui = em_ratio(r.data, r.size);
  return true;
}
} // namespace

bool init(const InitOptions &options) {
  IM_ASSERT(ImGui::GetCurrentContext() && "silta::init needs an ImGui context");
  State &st = S();
  st.theme = options.theme;
  st.scale = options.scale > 0 ? options.scale : 1.f;
  st.style_imgui = options.style_imgui;
  st.direction = options.direction;
  const bool loaded = load_fonts(options);
  if (st.style_imgui) style_imgui();
  request_frames(2);
  return loaded;
}

void shutdown() { detail::release_context(ImGui::GetCurrentContext()); }

namespace detail {
const FaceData &inter_face(int weight) { return S().inter[std::clamp(weight, 0, 2)]; }
const std::vector<FaceData> &fallback_faces() { return S().fallback; }
} // namespace detail

void style_imgui() {
  if (!ImGui::GetCurrentContext()) return;
  State &st = S();
  st.style_imgui = true;
  const Theme &t = st.theme;
  ImGuiStyle &s = ImGui::GetStyle();
  s = ImGuiStyle();
  s.WindowPadding = ImVec2(0, 0);
  s.WindowBorderSize = 0;
  s.WindowRounding = 0;
  s.FramePadding = ImVec2(0, 0);
  s.FrameBorderSize = 0;
  s.ItemSpacing = ImVec2(0, 0);
  s.ItemInnerSpacing = ImVec2(0, 0);
  s.ScrollbarSize = 10;
  s.ScrollbarRounding = 9999;
  s.PopupRounding = t.radius_md();
  s.DisabledAlpha = 0.5f; // shadcn: disabled:opacity-50
  s.ScaleAllSizes(st.scale);
  s.FontScaleDpi = st.scale;
  s.FontSizeBase = 14.f * st.fonts.em_to_imgui;
  if (st.fonts.regular) ImGui::GetIO().FontDefault = st.fonts.regular;

  auto set = [&](ImGuiCol idx, Color c) { s.Colors[idx] = c.vec4(); };
  set(ImGuiCol_Text, t.foreground);
  set(ImGuiCol_TextDisabled, t.muted_foreground);
  set(ImGuiCol_WindowBg, t.background);
  set(ImGuiCol_ChildBg, transparent);
  set(ImGuiCol_PopupBg, t.popover);
  set(ImGuiCol_Border, t.border);
  set(ImGuiCol_FrameBg, transparent);
  set(ImGuiCol_FrameBgHovered, transparent);
  set(ImGuiCol_FrameBgActive, transparent);
  set(ImGuiCol_ScrollbarBg, transparent);
  set(ImGuiCol_ScrollbarGrab, t.border);
  set(ImGuiCol_ScrollbarGrabHovered, mix(t.border, t.muted_foreground, 0.4f));
  set(ImGuiCol_ScrollbarGrabActive, t.muted_foreground);
  set(ImGuiCol_TextSelectedBg, t.ring.fade(0.35f));
  set(ImGuiCol_InputTextCursor, t.foreground);
  set(ImGuiCol_NavCursor, transparent);              // Silta draws its own focus rings
  set(ImGuiCol_ModalWindowDimBg, transparent);       // Silta's Dialog draws its own overlay
}

void begin_frame() {
  State &st = S();
  // A wake-up that is due has done its job; components ask again if needed.
  if (st.wake_at >= 0 && ImGui::GetTime() >= st.wake_at) st.wake_at = -1;
  detail::collect_textures();
  detail::overlay_begin_frame();
  anim::begin_frame();
  layout::begin_frame();
}

void end_frame() {
  layout::end_frame();
  anim::end_frame();
  State &st = S();
  if (st.pending_frames > 0) --st.pending_frames;
  if (layout::settling()) st.pending_frames = std::max(st.pending_frames, 1);
}

const Theme &theme() { return S().theme; }

void set_theme(const Theme &theme) {
  S().theme = theme;
  if (S().style_imgui) style_imgui();
  request_frames(2);
}

Direction direction() { return S().direction; }

void set_direction(Direction value) {
  State &st = S();
  if (st.direction == value) return;
  st.direction = value;
  request_frames(2);
}

bool is_rtl() { return S().direction == Direction::rtl; }

float scale() { return S().scale; }

void set_scale(float scale) {
  State &st = S();
  if (scale <= 0 || scale == st.scale) return;
  st.scale = scale;
  if (st.style_imgui) style_imgui();
  request_frames(2);
}

const Fonts &fonts() { return S().fonts; }

bool wants_frame() { return S().pending_frames > 0 || anim::running() || layout::settling(); }

void request_frames(int count) {
  State &st = S();
  st.pending_frames = std::max(st.pending_frames, count);
}

void wake_in(double seconds) {
  if (!ImGui::GetCurrentContext()) return;
  State &st = S();
  const double at = ImGui::GetTime() + std::max(0.0, seconds);
  if (st.wake_at < 0 || at < st.wake_at) st.wake_at = at;
}

double idle_timeout() {
  if (!ImGui::GetCurrentContext()) return -1;
  const State &st = S();
  double timeout = ImGui::GetIO().WantTextInput ? 0.1 : -1; // caret blink
  if (st.wake_at >= 0) {
    const double until = std::max(0.0, st.wake_at - ImGui::GetTime());
    timeout = timeout < 0 ? until : std::min(timeout, until);
  }
  return timeout;
}

#define SILTA_STR2(x) #x
#define SILTA_STR(x) SILTA_STR2(x)
const char *version() {
  return SILTA_STR(SILTA_VERSION_MAJOR) "." SILTA_STR(SILTA_VERSION_MINOR) "." SILTA_STR(SILTA_VERSION_PATCH);
}

} // namespace silta
