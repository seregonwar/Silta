// Uses the installed Silta package headlessly: the built-in fonts load, the
// host's ImGui style is left alone, and a frame of components renders.
#include "silta/silta.hpp"
#include <cstdio>
#include <cstring>

int main() {
  ImGui::CreateContext();
  ImGuiIO &io = ImGui::GetIO();
  io.IniFilename = nullptr;
  io.DisplaySize = {800, 600};
  io.DeltaTime = 1.f / 60;
  io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
  ImGui::GetPlatformIO().Renderer_TextureMaxWidth = 4096;
  ImGui::GetPlatformIO().Renderer_TextureMaxHeight = 4096;

  ImGui::StyleColorsLight();
  const ImVec4 host_window_bg = ImGui::GetStyle().Colors[ImGuiCol_WindowBg];

  if (!silta::init({.theme = silta::Theme::dark()})) {
    std::fprintf(stderr, "built-in fonts did not load\n");
    return 1;
  }
  if (std::memcmp(&host_window_bg, &ImGui::GetStyle().Colors[ImGuiCol_WindowBg], sizeof host_window_bg) != 0) {
    std::fprintf(stderr, "silta::init changed the host's ImGui style\n");
    return 1;
  }
  for (int frame = 0; frame < 3; ++frame) {
    ImGui::NewFrame();
    silta::begin_frame();
    ImGui::Begin("host window");
    ImGui::Text("Host ImGui widget");
    {
      auto card = silta::Card({.width = 300});
      silta::CardTitle("Silta inside a host");
      silta::Button("Continue");
    }
    ImGui::End();
    silta::end_frame();
    ImGui::Render();
    for (ImTextureData *t : ImGui::GetPlatformIO().Textures)
      if (t->Status == ImTextureStatus_WantCreate || t->Status == ImTextureStatus_WantUpdates)
        t->SetTexID(1), t->SetStatus(ImTextureStatus_OK);
  }
  if (ImGui::GetDrawData()->TotalVtxCount == 0) {
    std::fprintf(stderr, "nothing was drawn\n");
    return 1;
  }
  ImGui::DestroyContext();
  std::printf("Silta %s package OK\n", silta::version());
  return 0;
}
