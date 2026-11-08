// Silta built against the host's ImGui target renders a frame.
#include "silta/silta.hpp"
#include <cstdio>

int main() {
  ImGui::CreateContext();
  ImGuiIO &io = ImGui::GetIO();
  io.IniFilename = nullptr;
  io.DisplaySize = {640, 480};
  io.DeltaTime = 1.f / 60;
  io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
  ImGui::GetPlatformIO().Renderer_TextureMaxWidth = 4096;
  ImGui::GetPlatformIO().Renderer_TextureMaxHeight = 4096;
  if (!silta::init()) return 1;
  for (int i = 0; i < 3; ++i) {
    ImGui::NewFrame();
    silta::begin_frame();
    ImGui::Begin("host");
    silta::Button("Silta on the host's ImGui");
    ImGui::End();
    silta::end_frame();
    ImGui::Render();
    for (ImTextureData *t : ImGui::GetPlatformIO().Textures)
      if (t->Status == ImTextureStatus_WantCreate || t->Status == ImTextureStatus_WantUpdates)
        t->SetTexID(1), t->SetStatus(ImTextureStatus_OK);
  }
  ImGui::DestroyContext();
  std::printf("Silta on host ImGui %s OK\n", IMGUI_VERSION);
  return 0;
}
