// Renderer-free regression tests. Exceptions keep checks active in Release.
#include "silta/silta.hpp"
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
  try { color_tests(); theme_tests(); ui_tests(); layout_tests(); multi_context_tests(); std::cout << "Core tests passed\n"; return 0; }
  catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
