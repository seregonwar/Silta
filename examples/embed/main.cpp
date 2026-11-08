// Silta inside an application that owns its window, renderer and ImGui setup.
//
// The host here is the classic Dear ImGui Win32 + Direct3D 11 program with
// ImGui's own dark style. Silta is added with four calls and draws a panel in
// one of the host's ImGui windows; the host's widgets keep their look.
//
//   silta::init(...)                       once, after ImGui::CreateContext()
//   silta::begin_frame() / end_frame()     inside the host's ImGui frame
//   silta::wants_frame(), idle_timeout()   optional: render only when needed
#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"
#include "silta/silta.hpp"
#include "../../src/app/png.hpp" // tiny PNG writer, for --screenshot
#include <d3d11.h>
#include <shellapi.h>
#include <string>
#include <vector>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace {
ID3D11Device *device = nullptr;
ID3D11DeviceContext *context = nullptr;
IDXGISwapChain *swapchain = nullptr;
ID3D11RenderTargetView *target = nullptr;
UINT resize_w = 0, resize_h = 0;

void create_target() {
  ID3D11Texture2D *back = nullptr;
  swapchain->GetBuffer(0, IID_PPV_ARGS(&back));
  device->CreateRenderTargetView(back, nullptr, &target);
  back->Release();
}

bool create_device(HWND hwnd) {
  DXGI_SWAP_CHAIN_DESC sd = {};
  sd.BufferCount = 2;
  sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  sd.OutputWindow = hwnd;
  sd.SampleDesc.Count = 1;
  sd.Windowed = TRUE;
  sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
  const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0};
  if (FAILED(D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 1,
                                           D3D11_SDK_VERSION, &sd, &swapchain, &device, nullptr, &context)))
    return false;
  create_target();
  return true;
}

LRESULT WINAPI wnd_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
  if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wparam, lparam)) return true;
  switch (msg) {
  case WM_SIZE:
    if (wparam != SIZE_MINIMIZED) resize_w = LOWORD(lparam), resize_h = HIWORD(lparam);
    return 0;
  case WM_DESTROY: PostQuitMessage(0); return 0;
  }
  return DefWindowProcW(hwnd, msg, wparam, lparam);
}

// --screenshot <file.png>: render a few frames in a hidden window, save the
// back buffer and exit (used to check the example without showing a window).
void save_back_buffer(const wchar_t *path) {
  ID3D11Texture2D *back = nullptr, *staging = nullptr;
  swapchain->GetBuffer(0, IID_PPV_ARGS(&back));
  D3D11_TEXTURE2D_DESC desc;
  back->GetDesc(&desc);
  desc.Usage = D3D11_USAGE_STAGING, desc.BindFlags = 0, desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ, desc.MiscFlags = 0;
  device->CreateTexture2D(&desc, nullptr, &staging);
  context->CopyResource(staging, back);
  D3D11_MAPPED_SUBRESOURCE map;
  if (SUCCEEDED(context->Map(staging, 0, D3D11_MAP_READ, 0, &map))) {
    std::vector<uint8_t> rgba(size_t(desc.Width) * desc.Height * 4);
    for (UINT y = 0; y < desc.Height; ++y)
      memcpy(rgba.data() + size_t(y) * desc.Width * 4, static_cast<uint8_t *>(map.pData) + size_t(y) * map.RowPitch,
             size_t(desc.Width) * 4);
    for (size_t i = 3; i < rgba.size(); i += 4) rgba[i] = 255;
    context->Unmap(staging, 0);
    char utf8[MAX_PATH * 3];
    WideCharToMultiByte(CP_UTF8, 0, path, -1, utf8, sizeof utf8, nullptr, nullptr);
    silta::detail::write_png(utf8, rgba.data(), int(desc.Width), int(desc.Height));
  }
  staging->Release();
  back->Release();
}

// The host's state and Silta's state, side by side.
struct Settings {
  bool autosave = true;
  float quality = 0.8f;
  int format = 0;
  std::string name = "Untitled";
};

void silta_panel(Settings &s) {
  using namespace silta;
  if (auto card = Card({.width = fill})) {
    {
      auto header = CardHeader();
      CardTitle("Export settings");
      CardDescription("A Silta card inside the host's ImGui window.");
    }
    {
      auto content = CardContent({.gap = 12});
      Input("name", &s.name, {.placeholder = "File name"});
      static const char *const formats[] = {"MP4 (H.264)", "WebM (VP9)", "GIF"};
      Select("format", &s.format, formats, {.width = fill});
      Slider("quality", &s.quality, 0, 1);
      Switch("Autosave", &s.autosave);
    }
    {
      auto footer = CardFooter({.gap = 8, .justify = Justify::end});
      Button("Cancel", {.variant = Variant::outline});
      if (Button("Export")) toast("Export started", {.description = s.name.c_str()});
    }
  }
}
} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
  ImGui_ImplWin32_EnableDpiAwareness();
  const float dpi = ImGui_ImplWin32_GetDpiScaleForMonitor(MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY));
  WNDCLASSEXW wc = {sizeof(wc), CS_CLASSDC, wnd_proc, 0, 0, instance, nullptr, nullptr, nullptr, nullptr,
                    L"SiltaEmbed", nullptr};
  RegisterClassExW(&wc);
  HWND hwnd = CreateWindowW(wc.lpszClassName, L"Host app + Silta", WS_OVERLAPPEDWINDOW, 100, 100,
                            int(1100 * dpi), int(720 * dpi), nullptr, nullptr, instance, nullptr);
  if (!create_device(hwnd)) return 1;
  int argc = 0;
  wchar_t **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  const wchar_t *screenshot = argc >= 3 && wcscmp(argv[1], L"--screenshot") == 0 ? argv[2] : nullptr;
  if (!screenshot) ShowWindow(hwnd, SW_SHOWDEFAULT);

  // ---- The host's ImGui setup, untouched by Silta.
  ImGui::CreateContext();
  ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  if (screenshot) ImGui::GetIO().IniFilename = nullptr; // leave no imgui.ini behind
  ImGui::StyleColorsDark();
  ImGui::GetStyle().ScaleAllSizes(dpi);
  ImGui::GetStyle().FontScaleDpi = dpi;
  ImGui_ImplWin32_Init(hwnd);
  ImGui_ImplDX11_Init(device, context);

  // ---- Silta: one call. It loads its built-in fonts and keeps its own theme.
  silta::init({.theme = silta::Theme::dark(silta::BaseColor::zinc), .scale = dpi});

  Settings settings;
  bool show_demo = true;
  int frames = 0;
  for (bool running = true; running;) {
    // Render on demand: wait for input unless Silta (or the host) animates.
    if (!silta::wants_frame() && !show_demo && !screenshot) {
      const double timeout = silta::idle_timeout();
      MsgWaitForMultipleObjects(0, nullptr, FALSE, timeout < 0 ? INFINITE : DWORD(timeout * 1000), QS_ALLINPUT);
    }
    for (MSG msg; PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE);) {
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
      if (msg.message == WM_QUIT) running = false;
    }
    if (!running) break;
    if (resize_w && resize_h) {
      target->Release();
      swapchain->ResizeBuffers(0, resize_w, resize_h, DXGI_FORMAT_UNKNOWN, 0);
      resize_w = resize_h = 0;
      create_target();
    }

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    silta::begin_frame();

    // The host's own ImGui UI, in ImGui's style.
    if (show_demo) ImGui::ShowDemoWindow(&show_demo);
    ImGui::SetNextWindowSize(ImVec2(420 * dpi, 520 * dpi), ImGuiCond_FirstUseEver);
    ImGui::Begin("Inspector (host window)");
    ImGui::Text("ImGui widgets keep the host's style:");
    ImGui::Checkbox("Show ImGui demo", &show_demo);
    ImGui::Separator();
    silta_panel(settings); // Silta components, in Silta's theme
    ImGui::End();
    silta::Toaster();

    silta::end_frame();
    ImGui::Render();
    const float clear[4] = {0.07f, 0.07f, 0.08f, 1};
    context->OMSetRenderTargets(1, &target, nullptr);
    context->ClearRenderTargetView(target, clear);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    if (screenshot && ++frames == 30) {
      save_back_buffer(screenshot);
      running = false;
    }
    swapchain->Present(1, 0);
  }
  LocalFree(argv);

  ImGui_ImplDX11_Shutdown();
  ImGui_ImplWin32_Shutdown();
  ImGui::DestroyContext(); // also frees Silta's state for this context
  target->Release();
  swapchain->Release();
  context->Release();
  device->Release();
  DestroyWindow(hwnd);
  UnregisterClassW(wc.lpszClassName, wc.hInstance);
  return 0;
}
