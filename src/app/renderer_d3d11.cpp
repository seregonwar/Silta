// Direct3D 11 renderer: flip-model swapchain on the GLFW window's HWND.
#include "renderer.hpp"
#include "imgui.h"
#include "imgui_impl_dx11.h"
#include <d3d11.h>
#include <dxgi1_2.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#include <cstdio>
#include <iterator>

namespace silta::detail {
namespace {
template <class T> void release(T *&p) {
  if (p) p->Release();
  p = nullptr;
}

class D3D11Renderer final : public Renderer {
  GLFWwindow *window_ = nullptr;
  ID3D11Device *device_ = nullptr;
  ID3D11DeviceContext *context_ = nullptr;
  IDXGISwapChain1 *swapchain_ = nullptr;
  ID3D11RenderTargetView *target_ = nullptr;
  int width_ = 0, height_ = 0;
  bool occluded_ = false;

  void create_target() {
    ID3D11Texture2D *back = nullptr;
    if (SUCCEEDED(swapchain_->GetBuffer(0, IID_PPV_ARGS(&back)))) {
      device_->CreateRenderTargetView(back, nullptr, &target_);
      back->Release();
    }
  }

public:
  ~D3D11Renderer() override { shutdown(); }

  bool init(GLFWwindow *window) override {
    window_ = window;
    HWND hwnd = glfwGetWin32Window(window);
    glfwGetFramebufferSize(window, &width_, &height_);

    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
    UINT flags = 0;
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, levels, UINT(std::size(levels)),
                                   D3D11_SDK_VERSION, &device_, nullptr, &context_);
    if (hr == DXGI_ERROR_UNSUPPORTED) // no GPU: WARP software rasterizer
      hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, levels, UINT(std::size(levels)),
                             D3D11_SDK_VERSION, &device_, nullptr, &context_);
    if (FAILED(hr)) {
      std::fprintf(stderr, "silta: D3D11CreateDevice failed (0x%08lx)\n", hr);
      return false;
    }

    IDXGIDevice *dxgi_device = nullptr;
    IDXGIAdapter *adapter = nullptr;
    IDXGIFactory2 *factory = nullptr;
    device_->QueryInterface(IID_PPV_ARGS(&dxgi_device));
    if (dxgi_device) dxgi_device->GetAdapter(&adapter);
    if (adapter) adapter->GetParent(IID_PPV_ARGS(&factory));
    if (factory) {
      DXGI_SWAP_CHAIN_DESC1 sd = {};
      sd.Width = UINT(width_);
      sd.Height = UINT(height_);
      sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
      sd.SampleDesc.Count = 1;
      sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
      sd.BufferCount = 2;
      sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
      sd.Scaling = DXGI_SCALING_NONE;
      hr = factory->CreateSwapChainForHwnd(device_, hwnd, &sd, nullptr, nullptr, &swapchain_);
      if (FAILED(hr)) { // older systems: stretch scaling
        sd.Scaling = DXGI_SCALING_STRETCH;
        hr = factory->CreateSwapChainForHwnd(device_, hwnd, &sd, nullptr, nullptr, &swapchain_);
      }
      factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);
    }
    release(factory);
    release(adapter);
    release(dxgi_device);
    if (!swapchain_) {
      std::fprintf(stderr, "silta: D3D11 swapchain creation failed (0x%08lx)\n", hr);
      shutdown();
      return false;
    }
    create_target();
    return ImGui_ImplDX11_Init(device_, context_);
  }

  void new_frame() override {
    int w = 0, h = 0;
    glfwGetFramebufferSize(window_, &w, &h);
    if (w > 0 && h > 0 && (w != width_ || h != height_)) {
      release(target_);
      swapchain_->ResizeBuffers(0, UINT(w), UINT(h), DXGI_FORMAT_UNKNOWN, 0);
      width_ = w, height_ = h;
      create_target();
    }
    ImGui_ImplDX11_NewFrame();
  }

  void render(ImDrawData *data, Color clear, Capture *capture) override {
    if (!capture && occluded_ && swapchain_->Present(0, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED) return;
    occluded_ = false;
    const float c[4] = {clear.r * clear.a, clear.g * clear.a, clear.b * clear.a, clear.a};
    context_->OMSetRenderTargets(1, &target_, nullptr);
    context_->ClearRenderTargetView(target_, c);
    ImGui_ImplDX11_RenderDrawData(data);
    if (capture) read_back(*capture); // before Present: flip-discard drops the buffer
    occluded_ = swapchain_->Present(1, 0) == DXGI_STATUS_OCCLUDED;
  }

  void read_back(Capture &out) {
    ID3D11Texture2D *back = nullptr, *staging = nullptr;
    if (FAILED(swapchain_->GetBuffer(0, IID_PPV_ARGS(&back)))) return;
    D3D11_TEXTURE2D_DESC desc;
    back->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.MiscFlags = 0;
    if (SUCCEEDED(device_->CreateTexture2D(&desc, nullptr, &staging))) {
      context_->CopyResource(staging, back);
      D3D11_MAPPED_SUBRESOURCE map;
      if (SUCCEEDED(context_->Map(staging, 0, D3D11_MAP_READ, 0, &map))) {
        out.width = int(desc.Width), out.height = int(desc.Height);
        out.rgba.resize(size_t(out.width) * out.height * 4);
        for (int y = 0; y < out.height; ++y) {
          const uint8_t *src = static_cast<const uint8_t *>(map.pData) + size_t(y) * map.RowPitch;
          uint8_t *dst = out.rgba.data() + size_t(y) * out.width * 4;
          for (int x = 0; x < out.width; ++x) {
            dst[x * 4 + 0] = src[x * 4 + 0];
            dst[x * 4 + 1] = src[x * 4 + 1];
            dst[x * 4 + 2] = src[x * 4 + 2];
            dst[x * 4 + 3] = 255;
          }
        }
        context_->Unmap(staging, 0);
      }
    }
    release(staging);
    release(back);
  }

  void shutdown() override {
    if (device_ && ImGui::GetCurrentContext() && ImGui::GetIO().BackendRendererUserData)
      ImGui_ImplDX11_Shutdown();
    if (context_) context_->ClearState();
    release(target_);
    release(swapchain_);
    if (context_) context_->Flush();
    release(context_);
    release(device_);
  }
};
} // namespace

std::unique_ptr<Renderer> make_d3d11_renderer() { return std::make_unique<D3D11Renderer>(); }

} // namespace silta::detail
