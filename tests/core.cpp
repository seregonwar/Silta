// Renderer-free regression tests. Exceptions keep checks active in Release.
#include "silta/silta.hpp"
#include "../src/text_edit.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}
void near(float actual, float expected, const char *message) {
  require(std::abs(actual - expected) < 0.002f, message);
}
void color_tests() {
  using namespace silta;
  auto red = Color::hex(0xff0000, 0.5f);
  near(red.r, 1, "hex red"); near(red.g, 0, "hex green"); near(red.a, 0.5f, "hex alpha");
  auto result = over(red, Color::hex(0x0000ff));
  near(result.r, 0.5f, "source-over red"); near(result.b, 0.5f, "source-over blue");
  near(result.a, 1, "source-over alpha");
  require(over(transparent, transparent) == transparent, "transparent compositing");
  near(Color::oklch(1, 0, 0).r, 1, "OKLCH white");
  near(Color::oklch(0, 0, 0).r, 0, "OKLCH black");
  require(mix(black, white, -1) == black && mix(black, white, 2) == white, "mix bounds");
}
void theme_tests() {
  using namespace silta;
  for (auto base : {BaseColor::neutral, BaseColor::zinc, BaseColor::slate, BaseColor::stone, BaseColor::gray}) {
    auto light = Theme::light(base), dark = Theme::dark(base);
    require(!light.is_dark() && dark.is_dark(), "theme mode");
    require(light.background.r > dark.background.r, "light/dark backgrounds");
    auto primary = Color::hex(0x1d4ed8);
    auto branded = dark.with_primary(primary);
    require(branded.primary == primary, "brand accent");
    near(branded.ring.r, primary.r, "brand ring red");
    near(branded.ring.g, primary.g, "brand ring green");
    near(branded.ring.b, primary.b, "brand ring blue");
    require(branded.ring.a > 0 && branded.ring.a <= 1, "brand ring visibility");
    require(branded.background == dark.background, "branding preserves base theme");
    near(light.radius_sm(), light.radius - 4, "small radius");
  }
}
void ui_tests() {
  using namespace silta;
  ImGui::CreateContext();
  require(init({.theme = Theme::dark()}), "Silta initialization");
  auto &io = ImGui::GetIO();
  io.IniFilename = nullptr; io.DisplaySize = {1024, 768}; io.DeltaTime = 1.f / 60;
  // Honor ImGui 1.92's texture lifecycle without allocating a GPU resource.
  io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
  ImGui::GetPlatformIO().Renderer_TextureMaxWidth = 4096;
  ImGui::GetPlatformIO().Renderer_TextureMaxHeight = 4096;
  require(fonts().regular != nullptr, "bundled font loading");
  // With system fallback fonts, CJK characters are covered (CI images without
  // such fonts report no fallbacks and skip the check).
  if (fonts().fallback_count > 0)
    require(fonts().regular->IsGlyphInFont(0x65E5) || fonts().regular->IsGlyphInFont(0x0416),
            "fallback fonts cover characters Inter lacks");
  for (int frame = 0; frame < 8; ++frame) {
    ImGui::NewFrame(); begin_frame();
    ImGui::SetNextWindowSize({800, 600}); ImGui::Begin("test");
    {
      auto column = Column({.gap = 8, .width = 400});
      Text("Silta test", {.weight = Weight::semibold});
      Button("Continue");
      Button("Unavailable", {.disabled = true});
      Badge("Ready");
      Separator();
      bool checked = true; Checkbox("Enabled", &checked);
      float value = 0.5f; Slider("value", &value, 0, 1);
    }
    ImGui::End(); end_frame(); ImGui::Render();
    auto *data = ImGui::GetDrawData();
    require(data && data->Valid && data->TotalVtxCount > 0, "UI produces draw commands");
    for (auto *texture : *data->Textures) {
      if (texture->Status == ImTextureStatus_WantCreate || texture->Status == ImTextureStatus_WantUpdates) {
        require(texture->Pixels && texture->Width > 0, "font pixels");
        texture->SetTexID(1); texture->SetStatus(ImTextureStatus_OK);
      } else if (texture->Status == ImTextureStatus_WantDestroy) texture->SetStatus(ImTextureStatus_Destroyed);
    }
  }
  shutdown(); ImGui::DestroyContext();
}

// A row of auto height measures its stretched children by their content
// (a stretched column taller than the row makes the row grow, then shrink).
void layout_tests() {
  using namespace silta;
  ImGuiContext *ctx = ImGui::CreateContext();
  auto &io = ImGui::GetIO();
  io.IniFilename = nullptr; io.DisplaySize = {800, 600}; io.DeltaTime = 1.f / 60;
  io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
  require(init({.system_fallback = false}), "layout test init");
  float content = 120, measured = 0;
  auto run = [&] {
    for (int frame = 0; frame < 6; ++frame) {
      ImGui::NewFrame(); begin_frame();
      ImGui::SetNextWindowSize({600, 500}); ImGui::Begin("layout");
      {
        auto row = Row({.gap = 8});
        { auto col = Column(); layout::allocate(ImVec2(50, content)); }
        { auto col = Column(); layout::allocate(ImVec2(50, 20)); }
        measured = row.rect().GetHeight();
      }
      ImGui::End(); end_frame(); ImGui::Render();
      for (auto *t : ImGui::GetPlatformIO().Textures)
        if (t->Status == ImTextureStatus_WantCreate || t->Status == ImTextureStatus_WantUpdates)
          t->SetTexID(1), t->SetStatus(ImTextureStatus_OK);
    }
  };
  run();
  near(measured, content, "stretched children grow an auto-height row");
  content = 40;
  run();
  near(measured, content, "and shrink it again");
  ImGui::DestroyContext(ctx);
}

// One frame of Silta in the current context; also acknowledges the texture
// requests like a renderer backend would.
void frame(const char *window) {
  using namespace silta;
  ImGui::NewFrame(); begin_frame();
  ImGui::SetNextWindowSize({600, 400}); ImGui::Begin(window);
  { auto column = Column({.gap = 8, .width = 300}); Text(window); Button("Go"); bool on = true; Switch("On", &on); }
  toast("hello");
  Toaster();
  ImGui::End(); end_frame(); ImGui::Render();
  for (auto *texture : ImGui::GetPlatformIO().Textures) {
    if (texture->Status == ImTextureStatus_WantCreate || texture->Status == ImTextureStatus_WantUpdates)
      texture->SetTexID(1), texture->SetStatus(ImTextureStatus_OK);
    else if (texture->Status == ImTextureStatus_WantDestroy) texture->SetTexID(ImTextureID_Invalid), texture->SetStatus(ImTextureStatus_Destroyed);
  }
}

ImGuiContext *make_context() {
  ImGuiContext *ctx = ImGui::CreateContext();
  ImGui::SetCurrentContext(ctx);
  auto &io = ImGui::GetIO();
  io.IniFilename = nullptr; io.DisplaySize = {800, 600}; io.DeltaTime = 1.f / 60;
  io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
  ImGui::GetPlatformIO().Renderer_TextureMaxWidth = 4096;
  ImGui::GetPlatformIO().Renderer_TextureMaxHeight = 4096;
  return ctx;
}

// Right-to-left layout mirrors rows, cross-axis alignment and justification.
void rtl_tests() {
  using namespace silta;
  ImGuiContext *ctx = make_context();
  require(init({.system_fallback = false, .direction = Direction::rtl}), "rtl init");
  require(is_rtl(), "direction is right-to-left");
  float first = 0, second = 0, aligned = 0, stretched = 0, end_first = 0, end_second = 0;
  for (int frame = 0; frame < 4; ++frame) {
    ImGui::NewFrame(); begin_frame();
    ImGui::SetNextWindowSize({600, 500}); ImGui::Begin("rtl");
    {
      auto row = Row({.width = 300, .height = 20});
      const ImRect a = layout::allocate(ImVec2(100, 20));
      const ImRect b = layout::allocate(ImVec2(50, 20));
      const float origin = row.rect().Min.x;
      first = a.Min.x - origin;
      second = b.Min.x - origin;
    }
    {
      auto col = Column({.align = Align::start, .width = 300, .height = 20});
      const ImRect child = layout::allocate(ImVec2(100, 20));
      aligned = child.Min.x - col.rect().Min.x;
    }
    {
      auto col = Column({.width = 300, .height = 20}); // stretch: a fixed child keeps its width
      const ImRect child = layout::allocate(ImVec2(100, 20));
      stretched = child.Min.x - col.rect().Min.x;
    }
    {
      auto row = Row({.justify = Justify::end, .width = 300, .height = 20});
      const ImRect a = layout::allocate(ImVec2(100, 20));
      const ImRect b = layout::allocate(ImVec2(50, 20));
      const float origin = row.rect().Min.x;
      end_first = a.Min.x - origin;
      end_second = b.Min.x - origin;
    }
    ImGui::End(); end_frame(); ImGui::Render();
    for (auto *t : ImGui::GetPlatformIO().Textures)
      if (t->Status == ImTextureStatus_WantCreate || t->Status == ImTextureStatus_WantUpdates)
        t->SetTexID(1), t->SetStatus(ImTextureStatus_OK);
  }
  near(first, 200, "RTL rows start at the right");
  near(second, 150, "RTL rows advance left");
  near(aligned, 200, "RTL column start aligns to the right");
  near(stretched, 200, "RTL stretch places fixed-width children at the right");
  near(end_first, 50, "RTL justify end packs to the left");
  near(end_second, 0, "RTL justify end reaches the left edge");
  set_direction(Direction::ltr);
  require(!is_rtl(), "direction changes at runtime");
  ImGui::DestroyContext(ctx);
}

// Shaped fields map logical byte offsets to visual positions: RTL starts on
// the right, arrows follow the visual order and mixed runs keep their bidi
// order. Also covers wrapped Textarea lines and the unchanged LTR path.
void shaped_edit_tests() {
  using namespace silta;
  ImGuiContext *ctx = make_context();
  require(init({.system_fallback = false}), "shaped edit init");
  if (!detail::shaped_editing_available()) {
    ImGui::DestroyContext(ctx);
    return;
  }
  const float fs = imgui_font_size(font_points(TextSize::sm));
  const std::string arabic = "مرحبا";
  detail::EditLayout layout;
  detail::layout_edit_text(arabic.c_str(), arabic.size(), 0, fs, true, false, 0, 400, layout);
  require(layout.lines.size() == 1, "one RTL line");
  const detail::CaretPosition start = detail::caret_at(layout, 0, layout.content_width, 0);
  const detail::CaretPosition end = detail::caret_at(layout, arabic.size(), 0, 0);
  require(start.x > end.x, "RTL logical start is on the right");
  const detail::CaretPosition left = detail::move_caret(layout, start, -1);
  require(left.offset > 0 && left.x < start.x, "visual left advances into RTL text");
  const detail::CaretPosition right = detail::move_caret(layout, left, 1);
  require(right.offset == 0 && right.x > left.x, "visual right returns to the start");
  // Selection mapping: the first logical character occupies the right edge.
  const auto &line = layout.lines[0];
  const auto first_cluster = std::find_if(line.clusters.begin(), line.clusters.end(),
                                          [](const detail::EditCluster &c) { return c.begin == 0; });
  require(first_cluster != line.clusters.end(), "first cluster exists");
  near(first_cluster->x1, line.width, "RTL selection starts at the right edge");

  // The embedded Latin run reads left-to-right even inside the RTL paragraph:
  // offset 0 is at its left edge, offset 3 at its right edge.
  const std::string mixed = "abc مرحبا 123";
  detail::layout_edit_text(mixed.c_str(), mixed.size(), 0, fs, true, false, 0, 600, layout);
  const detail::CaretPosition mixed_start = detail::caret_at(layout, 0, layout.content_width, 0);
  const detail::CaretPosition mixed_latin_end = detail::caret_at(layout, 3, layout.content_width, 0);
  require(mixed_start.x < mixed_latin_end.x, "Latin runs read left-to-right inside RTL");
  const detail::CaretPosition next = detail::move_caret(layout, mixed_start, 1);
  require(next.offset == 1 && next.x > mixed_start.x, "visual right follows the Latin run");
  const detail::CaretPosition prev = detail::move_caret(layout, next, -1);
  require(prev.offset == 0 && prev.x < next.x, "visual left follows the Latin run back");
  const detail::CaretPosition click = detail::caret_at_point(layout, layout.content_width - 1, 1);
  require(click.offset == 3, "clicking the right edge enters the last Latin character");

  // Clipboard editing works from logical byte ranges, so an RTL selection
  // (cursor at the visual left, anchor at the right) copies the value in
  // storage order and a paste replaces it as one edit.
  require(detail::selection_text(mixed, mixed.size(), 0) == mixed, "select-all copies the whole value");
  std::string pasted = mixed;
  std::size_t paste_cursor = pasted.size(), paste_anchor = 0;
  detail::replace_selection(pasted, paste_cursor, paste_anchor, "שלום");
  require(pasted == "שלום", "paste replaces an RTL selection");
  require(paste_cursor == pasted.size() && paste_anchor == paste_cursor,
          "paste leaves the caret after the inserted text");
  pasted = "abc";
  paste_cursor = paste_anchor = 1;
  detail::replace_selection(pasted, paste_cursor, paste_anchor, "مرحبا");
  require(pasted == "aمرحباbc" && paste_cursor == 11 && paste_anchor == 11,
          "paste at a caret inserts at the logical offset");

  std::string cut = mixed;
  std::size_t cut_cursor = cut.size(), cut_anchor = 0;
  require(detail::selection_text(cut, cut_cursor, cut_anchor) == mixed, "cut copies the selection");
  detail::erase_selection(cut, cut_cursor, cut_anchor);
  require(cut.empty() && cut_cursor == 0 && cut_anchor == 0,
          "cut clears the selection and puts the caret at its start");
  require(detail::sanitize_paste("a\r\nb\rc", true) == "a\nb\nc", "pasted CRLF normalized");
  require(detail::sanitize_paste("a\r\nb\tc", false) == "ab c",
          "single-line paste drops breaks and tabs");

  // Wrapped Textarea: lines align to the right and vertical movement lands on
  // the following line.
  const std::string long_arabic = "مرحبا بالعالم هذا نص طويل لاختبار التفاف الأسطر في منطقة النص";
  detail::layout_edit_text(long_arabic.c_str(), long_arabic.size(), 0, fs, true, true, 160, 160, layout);
  require(layout.lines.size() > 1, "RTL textarea wraps");
  for (const detail::EditLine &wrapped : layout.lines)
    require(wrapped.origin >= -0.5f && wrapped.origin + wrapped.width <= layout.content_width + 0.5f,
            "wrapped RTL lines stay inside the field");
  const detail::CaretPosition first = detail::caret_at(layout, 0, layout.content_width, 0);
  const detail::CaretPosition below = detail::move_line(layout, first, 1);
  require(below.line == 1, "down moves to the next wrapped line");

  // Backspace and Delete pick the neighbouring character on the visual side,
  // not the adjacent byte. At the caret after the first RTL character the
  // character on its visual left is the second one in storage.
  detail::layout_edit_text(arabic.c_str(), arabic.size(), 0, fs, true, false, 0, 400, layout);
  const detail::CaretPosition after_first = detail::caret_at(layout, 2, 0, 0);
  std::size_t range_begin = 1, range_end = 1;
  require(detail::visual_delete_range(arabic.c_str(), arabic.size(), layout, after_first, -1, range_begin, range_end),
          "RTL backspace finds a character");
  require(range_begin == 2 && range_end == 4, "RTL backspace removes the character on the visual left");
  require(detail::visual_delete_range(arabic.c_str(), arabic.size(), layout, after_first, 1, range_begin, range_end),
          "RTL delete finds a character");
  require(range_begin == 0 && range_end == 2, "RTL delete removes the character on the visual right");

  // A line break is one delete away from the start of the next paragraph.
  const std::string paragraphs = "abc\ndef";
  detail::layout_edit_text(paragraphs.c_str(), paragraphs.size(), 0, fs, false, true, 200, 200, layout);
  const detail::CaretPosition second_line = detail::caret_at(layout, 4, 0, 1);
  require(detail::visual_delete_range(paragraphs.c_str(), paragraphs.size(), layout, second_line, -1, range_begin, range_end),
          "backspace at a line start finds the break");
  require(range_begin == 3 && range_end == 4, "backspace at a line start removes the newline");

  // LTR editing still follows the bytes.
  detail::layout_edit_text("abc", 3, 0, fs, false, false, 0, 100, layout);
  const detail::CaretPosition ltr_mid = detail::caret_at(layout, 1, 0, 0);
  require(detail::visual_delete_range("abc", 3, layout, ltr_mid, -1, range_begin, range_end) && range_begin == 0 &&
              range_end == 1,
          "LTR backspace removes the previous byte");
  require(detail::visual_delete_range("abc", 3, layout, ltr_mid, 1, range_begin, range_end) && range_begin == 1 &&
              range_end == 2,
          "LTR delete removes the next byte");

  // IME composition keeps the field's value untouched while the preview is
  // shaped over the selection captured at composition start. Commit is one
  // edit; cancel restores exactly the pre-composition state.
  std::string composed = "abc";
  detail::ImeEdit ime;
  detail::ime_begin(ime, "\xE6\x97\xA5\xE6\x9C\xAC", 3, 3);
  require(composed == "abc", "composition preview does not edit the value");
  require(detail::ime_preview_text(composed, ime) == "abc\xE6\x97\xA5\xE6\x9C\xAC",
          "composition preview is inserted at the caret");
  require(detail::ime_preview_caret(ime) == 9, "composition caret follows the preview");
  detail::ime_update(ime, "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E");
  require(composed == "abc" && ime.preview == "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E",
          "composition update replaces only the preview");
  std::size_t ime_cursor = 3, ime_anchor = 3;
  require(detail::ime_commit(composed, ime, ime_cursor, ime_anchor), "composition commit reports an edit");
  require(composed == "abc\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E" && ime_cursor == composed.size() &&
              ime_anchor == ime_cursor,
          "composition commit inserts once and leaves the caret after it");
  require(!ime.active, "composition commit ends the composition");

  composed = "abc";
  detail::ime_begin(ime, "\xE6\xB8\xAC\xE8\xA9\xA6", 1, 3);
  detail::ime_cancel(ime);
  require(composed == "abc" && !ime.active, "composition cancel leaves the value untouched");

  composed = "hello world";
  detail::ime_begin(ime, "\xD9\x85\xD8\xB1\xD8\xAD\xD8\xA8\xD8\xA7", 6, 11);
  require(detail::ime_preview_text(composed, ime) == "hello \xD9\x85\xD8\xB1\xD8\xAD\xD8\xA8\xD8\xA7",
          "composition preview replaces the selection visually");
  require(detail::ime_preview_caret(ime) == 16, "composition caret is after the replacement");
  ime_cursor = 2;
  ime_anchor = 2;
  require(detail::ime_commit(composed, ime, ime_cursor, ime_anchor), "selection composition commits");
  require(composed == "hello \xD9\x85\xD8\xB1\xD8\xAD\xD8\xA8\xD8\xA7" && ime_cursor == composed.size() &&
              ime_anchor == ime_cursor,
          "composition replaces the selection as a single edit");

  composed = "abc";
  detail::ime_begin(ime, "", 1, 3);
  ime_cursor = 1;
  ime_anchor = 3;
  require(!detail::ime_commit(composed, ime, ime_cursor, ime_anchor), "empty composition commits nothing");
  require(composed == "abc" && ime_cursor == 1 && ime_anchor == 3,
          "empty composition preserves the selection");

  composed = "abc";
  detail::ime_begin(ime, "\xD9\x85\xD8\xB1\xD8\xAD\xD8\xA8\xD8\xA7 \xE6\x97\xA5\xE6\x9C\xAC", 3, 3);
  ime_cursor = ime_anchor = 3;
  require(detail::ime_commit(composed, ime, ime_cursor, ime_anchor), "mixed-script composition commits");
  require(composed == "abc\xD9\x85\xD8\xB1\xD8\xAD\xD8\xA8\xD8\xA7 \xE6\x97\xA5\xE6\x9C\xAC" &&
              ime_cursor == composed.size(),
          "mixed-script composition stores logical bytes and puts the caret after them");

  // Committing at the edges of a wrapped paragraph must keep the caret on the
  // line the user was editing, not snap it to the paragraph's other edge.
  std::string wrapped = "first line\nsecond line";
  detail::layout_edit_text(wrapped.c_str(), wrapped.size(), 0, fs, false, true, 400, 400, layout);
  detail::ime_begin(ime, "X", 11, 11);
  ime_cursor = ime_anchor = 11;
  require(detail::ime_commit(wrapped, ime, ime_cursor, ime_anchor), "composition commits at a wrapped line start");
  require(wrapped == "first line\nXsecond line" && ime_cursor == 12, "line-start commit inserts before the line");
  detail::layout_edit_text(wrapped.c_str(), wrapped.size(), 0, fs, false, true, 400, 400, layout);
  require(detail::caret_at(layout, ime_cursor, 0, -1).line == 1, "line-start caret stays on the second line");

  wrapped = "first line\nsecond line";
  detail::ime_begin(ime, "Y", 10, 10);
  ime_cursor = ime_anchor = 10;
  require(detail::ime_commit(wrapped, ime, ime_cursor, ime_anchor), "composition commits at a wrapped line end");
  require(wrapped == "first lineY\nsecond line" && ime_cursor == 11, "line-end commit inserts at the line end");
  detail::layout_edit_text(wrapped.c_str(), wrapped.size(), 0, fs, false, true, 400, 400, layout);
  require(detail::caret_at(layout, ime_cursor, 0, -1).line == 0, "line-end caret stays on the first line");

  // LTR ASCII is unchanged: logical and visual order agree.
  detail::layout_edit_text("abc", 3, 0, fs, false, false, 0, 100, layout);
  const detail::CaretPosition ltr0 = detail::caret_at(layout, 0, 0, 0);
  const detail::CaretPosition ltr1 = detail::move_caret(layout, ltr0, 1);
  require(ltr0.x < detail::caret_at(layout, 3, 0, 0).x, "LTR caret moves right with offsets");
  require(ltr1.offset == 1, "LTR arrow follows logical order");
  ImGui::DestroyContext(ctx);
}

// Two ImGui contexts run Silta side by side with their own state, and
// destroying one leaves the other working.
void multi_context_tests() {
  using namespace silta;
  ImGuiContext *a = make_context();
  ImGui::StyleColorsClassic();
  const ImVec4 host_text = ImGui::GetStyle().Colors[ImGuiCol_Text];
  require(init({.theme = Theme::dark()}), "context A init");
  require(ImGui::GetStyle().Colors[ImGuiCol_Text].x == host_text.x &&
              ImGui::GetStyle().Colors[ImGuiCol_Text].w == host_text.w,
          "init leaves the host's ImGui style alone");
  ImGuiContext *b = make_context();
  require(init({.theme = Theme::light(), .scale = 2.f}), "context B init");

  for (int i = 0; i < 6; ++i) {
    ImGui::SetCurrentContext(a); frame("A");
    ImGui::SetCurrentContext(b); frame("B");
  }
  ImGui::SetCurrentContext(a);
  require(theme().is_dark() && scale() == 1.f, "context A keeps its theme and scale");
  ImGui::SetCurrentContext(b);
  require(!theme().is_dark() && scale() == 2.f, "context B keeps its theme and scale");

  {
    // A texture outlives its context's destruction safely.
    ImGui::SetCurrentContext(a);
    Texture texture;
    const unsigned char pixel[4] = {255, 0, 0, 255};
    texture.update(pixel, 1, 1);
    ImGui::DestroyContext(a);
    ImGui::SetCurrentContext(b);
  }
  for (int i = 0; i < 4; ++i) frame("B after A");
  require(!theme().is_dark(), "context B unaffected by A's destruction");
  ImGui::DestroyContext(b);
}
}
int main() {
  try { color_tests(); theme_tests(); ui_tests(); layout_tests(); rtl_tests(); shaped_edit_tests(); multi_context_tests(); std::cout << "Core tests passed\n"; return 0; }
  catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
