#include "silta/context.hpp"
#include "silta/anim.hpp"
#include "silta/layout.hpp"
#include "silta/texture.hpp"
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

namespace silta {
namespace {
struct State {
  Theme theme = Theme::light();
  float scale = 1.f;
  Fonts fonts;
  int pending_frames = 2;
  bool initialized = false;
  double wake_at = -1; // ImGui time of the earliest requested wake-up
};
State state;

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
float em_ratio(const std::filesystem::path &file) {
  std::ifstream in(file, std::ios::binary);
  std::vector<unsigned char> d((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  if (d.size() < 12) return 1.f;
  const uint16_t tables = be16(&d[4]);
  int asc = 0, desc = 0, upm = 0;
  for (uint16_t i = 0; i < tables; ++i) {
    const size_t rec = 12 + size_t(i) * 16;
    if (rec + 16 > d.size()) break;
    const uint32_t off = be32(&d[rec + 8]);
    if (std::memcmp(&d[rec], "hhea", 4) == 0 && off + 8 <= d.size()) {
      asc = int16_t(be16(&d[off + 4]));
      desc = int16_t(be16(&d[off + 6]));
    } else if (std::memcmp(&d[rec], "head", 4) == 0 && off + 20 <= d.size()) {
      upm = be16(&d[off + 18]);
    }
  }
  return upm > 0 && asc - desc > 0 ? float(asc - desc) / float(upm) : 1.f;
}

void apply_imgui_style() {
  if (!ImGui::GetCurrentContext()) return;
  const Theme &t = state.theme;
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
  s.ScaleAllSizes(state.scale);
  s.FontScaleDpi = state.scale;
  s.FontSizeBase = 14.f * state.fonts.em_to_imgui;

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
  // Silta draws its own focus rings.
  set(ImGuiCol_NavCursor, transparent);
  set(ImGuiCol_ModalWindowDimBg, black.with_alpha(0.5f));
}

ImFont *load_font(const std::filesystem::path &dir, const char *file) {
  const std::filesystem::path path = dir / file;
  std::error_code ec;
  if (!std::filesystem::exists(path, ec)) return nullptr;
  ImFontConfig cfg;
#ifdef IMGUI_ENABLE_FREETYPE
  cfg.FontLoaderFlags = ImGuiFreeTypeLoaderFlags_LightHinting;
#endif
  cfg.PixelSnapH = true;
  std::snprintf(cfg.Name, sizeof cfg.Name, "%s", file);
  return ImGui::GetIO().Fonts->AddFontFromFileTTF(path.string().c_str(), 0.f, &cfg);
}
} // namespace

bool init(const InitOptions &options) {
  IM_ASSERT(ImGui::GetCurrentContext() && "silta::init needs an ImGui context");
  state.theme = options.theme;
  state.scale = options.scale > 0 ? options.scale : 1.f;

  std::vector<std::filesystem::path> dirs;
  if (options.font_dir) dirs.emplace_back(options.font_dir);
  dirs.push_back(executable_dir() / "fonts");
#ifdef SILTA_DEFAULT_FONT_DIR
  dirs.emplace_back(SILTA_DEFAULT_FONT_DIR);
#endif
  bool loaded = false;
  for (const auto &dir : dirs) {
    ImFont *regular = load_font(dir, "Inter-Regular.ttf");
    if (!regular) continue;
    state.fonts.regular = regular;
    state.fonts.medium = load_font(dir, "Inter-Medium.ttf");
    state.fonts.semibold = load_font(dir, "Inter-SemiBold.ttf");
    if (!state.fonts.medium) state.fonts.medium = regular;
    if (!state.fonts.semibold) state.fonts.semibold = state.fonts.medium;
    state.fonts.em_to_imgui = em_ratio(dir / "Inter-Regular.ttf");
    ImGui::GetIO().FontDefault = regular;
    loaded = true;
    break;
  }
  if (!loaded) {
    std::fprintf(stderr, "silta: Inter fonts not found, falling back to ImGui's default font\n");
    state.fonts = {};
    state.fonts.em_to_imgui = 1.f;
  }
  state.initialized = true;
  apply_imgui_style();
  return loaded;
}

void shutdown() { state = State{}; }

void begin_frame() {
  // A wake-up that is due has done its job; components ask again if needed.
  if (state.wake_at >= 0 && ImGui::GetTime() >= state.wake_at) state.wake_at = -1;
  detail::collect_textures();
  anim::begin_frame();
  layout::begin_frame();
}

void end_frame() {
  layout::end_frame();
  anim::end_frame();
  if (state.pending_frames > 0) --state.pending_frames;
  if (layout::settling()) state.pending_frames = std::max(state.pending_frames, 1);
}

const Theme &theme() { return state.theme; }

void set_theme(const Theme &theme) {
  state.theme = theme;
  apply_imgui_style();
  request_frames(2);
}

float scale() { return state.scale; }

void set_scale(float scale) {
  if (scale <= 0 || scale == state.scale) return;
  state.scale = scale;
  apply_imgui_style();
  request_frames(2);
}

const Fonts &fonts() { return state.fonts; }

bool wants_frame() { return state.pending_frames > 0 || anim::running() || layout::settling(); }

void request_frames(int count) { state.pending_frames = std::max(state.pending_frames, count); }

void wake_in(double seconds) {
  if (!ImGui::GetCurrentContext()) return;
  const double at = ImGui::GetTime() + std::max(0.0, seconds);
  if (state.wake_at < 0 || at < state.wake_at) state.wake_at = at;
}

double idle_timeout() {
  if (!ImGui::GetCurrentContext()) return -1;
  double timeout = ImGui::GetIO().WantTextInput ? 0.1 : -1; // caret blink
  if (state.wake_at >= 0) {
    const double until = std::max(0.0, state.wake_at - ImGui::GetTime());
    timeout = timeout < 0 ? until : std::min(timeout, until);
  }
  return timeout;
}

} // namespace silta
