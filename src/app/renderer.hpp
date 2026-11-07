#pragma once
// A renderer backend for the application shell (internal).
#include <cstdint>
#include <memory>
#include <vector>
#include "silta/color.hpp"

struct GLFWwindow;
struct ImDrawData;

namespace silta::detail {

// Pixels read back from the frame just rendered (RGBA8, top-down).
struct Capture {
  std::vector<uint8_t> rgba;
  int width = 0, height = 0;
};

class Renderer {
public:
  virtual ~Renderer() = default;
  // Creates the device and swapchain for `window` and initialises the ImGui
  // renderer backend.
  virtual bool init(GLFWwindow *window) = 0;
  virtual void new_frame() = 0;
  // Renders and presents. With `capture`, also reads the frame back.
  virtual void render(ImDrawData *data, Color clear, Capture *capture = nullptr) = 0;
  // Releases everything; the window can then be given to another renderer.
  virtual void shutdown() = 0;
};

#if SILTA_HAS_D3D11
std::unique_ptr<Renderer> make_d3d11_renderer();
#endif
#if SILTA_HAS_VULKAN
std::unique_ptr<Renderer> make_vulkan_renderer();
#endif

} // namespace silta::detail
