#include "silta/app.hpp"
#include "png.hpp"
#include "renderer.hpp"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_internal.h"
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace silta {

const char *backend_name(Backend backend) {
  switch (backend) {
  case Backend::automatic: return "Automatic";
  case Backend::d3d11: return "Direct3D 11";
  case Backend::vulkan: return "Vulkan";
  }
  return "?";
}

std::vector<Backend> available_backends() {
  std::vector<Backend> list;
#if SILTA_HAS_D3D11
  list.push_back(Backend::d3d11);
#endif
#if SILTA_HAS_VULKAN
  list.push_back(Backend::vulkan);
#endif
  return list;
}

namespace {
std::unique_ptr<detail::Renderer> make_renderer(Backend backend) {
  switch (backend) {
#if SILTA_HAS_D3D11
  case Backend::d3d11: return detail::make_d3d11_renderer();
#endif
#if SILTA_HAS_VULKAN
  case Backend::vulkan: return detail::make_vulkan_renderer();
#endif
  default: return nullptr;
  }
}
} // namespace

struct App::Impl {
  AppOptions options;
  GLFWwindow *window = nullptr;
  std::unique_ptr<detail::Renderer> renderer;
  Backend backend = Backend::automatic;
  Backend wanted = Backend::automatic;
  const std::function<void()> *frame = nullptr;
  int extra_frames = 2;
  bool in_frame = false;
  bool glfw_ready = false;
  bool input_seen = false;
  std::string screenshot_path; // pending screenshot
  // SILTA_TRACE=<file>: logs why each frame was rendered.
  std::FILE *trace = nullptr;

  // Starts `wanted`, or the first backend that works.
  bool start_renderer(Backend wanted_backend) {
    std::vector<Backend> order;
    if (wanted_backend != Backend::automatic) order.push_back(wanted_backend);
    for (Backend b : available_backends())
      if (b != wanted_backend) order.push_back(b);
    for (Backend b : order) {
      auto r = make_renderer(b);
      if (r && r->init(window)) {
        renderer = std::move(r);
        backend = wanted = b;
        return true;
      }
      std::fprintf(stderr, "silta: %s renderer failed, trying the next one\n", backend_name(b));
    }
    return false;
  }

  void sync_scale() {
    float sx = 1, sy = 1;
    glfwGetWindowContentScale(window, &sx, &sy);
    set_scale(sx);
  }

  void render_frame() {
    if (in_frame || !renderer) return;
    in_frame = true;
    if (wanted != backend) {
      renderer->shutdown();
      renderer.reset();
      if (!start_renderer(wanted)) {
        std::fprintf(stderr, "silta: no renderer could be started\n");
        glfwSetWindowShouldClose(window, GLFW_TRUE);
        in_frame = false;
        return;
      }
      extra_frames = 2;
    }
    renderer->new_frame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    begin_frame();

    if (options.root_window) {
      const ImGuiViewport *vp = ImGui::GetMainViewport();
      ImGui::SetNextWindowPos(vp->WorkPos);
      ImGui::SetNextWindowSize(vp->WorkSize);
      ImGui::Begin("##silta.root", nullptr,
                   ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings |
                       ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus);
    }
    if (frame && *frame) (*frame)();
    if (options.root_window) ImGui::End();

    end_frame();
    ImGui::Render();
    if (!screenshot_path.empty()) {
      detail::Capture capture;
      renderer->render(ImGui::GetDrawData(), theme().background, &capture);
      if (capture.rgba.empty() ||
          !detail::write_png(screenshot_path.c_str(), capture.rgba.data(), capture.width, capture.height))
        std::fprintf(stderr, "silta: screenshot %s failed\n", screenshot_path.c_str());
      screenshot_path.clear();
    } else {
      renderer->render(ImGui::GetDrawData(), theme().background);
    }
    in_frame = false;
  }
};

namespace {
App::Impl *impl_of(GLFWwindow *w) { return static_cast<App::Impl *>(glfwGetWindowUserPointer(w)); }
void mark(GLFWwindow *w) {
  if (App::Impl *p = impl_of(w)) p->input_seen = true;
}
} // namespace

App::App(const AppOptions &options) : impl_(std::make_unique<Impl>()) {
  Impl &a = *impl_;
  a.options = options;
  if (const char *path = std::getenv("SILTA_TRACE")) a.trace = std::fopen(path, "w");
  glfwSetErrorCallback([](int code, const char *text) { std::fprintf(stderr, "silta: GLFW %d: %s\n", code, text); });
  if (!glfwInit()) return;
  a.glfw_ready = true;

  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);
  glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
  a.window = glfwCreateWindow(options.width, options.height, options.title, nullptr, nullptr);
  if (!a.window) return;
  glfwSetWindowUserPointer(a.window, &a);

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO &io = ImGui::GetIO();
  io.IniFilename = nullptr;
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;

  InitOptions init_options = options.init;
  float sx = 1, sy = 1;
  glfwGetWindowContentScale(a.window, &sx, &sy);
  init_options.scale = sx;
  init(init_options);

  // Input callbacks installed before ImGui's: its backend chains to them, so
  // Silta knows when real input arrived (ImGui also polls the mouse and
  // gamepads every frame, which must not keep the loop awake).
  glfwSetCursorPosCallback(a.window, [](GLFWwindow *w, double, double) { mark(w); });
  glfwSetMouseButtonCallback(a.window, [](GLFWwindow *w, int, int, int) { mark(w); });
  glfwSetScrollCallback(a.window, [](GLFWwindow *w, double, double) { mark(w); });
  glfwSetKeyCallback(a.window, [](GLFWwindow *w, int, int, int, int) { mark(w); });
  glfwSetCharCallback(a.window, [](GLFWwindow *w, unsigned) { mark(w); });
  glfwSetCursorEnterCallback(a.window, [](GLFWwindow *w, int) { mark(w); });
  glfwSetWindowFocusCallback(a.window, [](GLFWwindow *w, int) { mark(w); });
  glfwSetFramebufferSizeCallback(a.window, [](GLFWwindow *w, int, int) { mark(w); });
  ImGui_ImplGlfw_InitForOther(a.window, true);
  if (!a.start_renderer(options.backend)) {
    std::fprintf(stderr, "silta: no renderer could be started\n");
    return;
  }

  // Keep drawing while Windows runs its modal move/resize loop.
  glfwSetWindowRefreshCallback(a.window, [](GLFWwindow *w) {
    if (Impl *p = impl_of(w)) p->render_frame();
  });
  glfwSetWindowContentScaleCallback(a.window, [](GLFWwindow *w, float, float) {
    if (Impl *p = impl_of(w)) p->sync_scale();
  });
  if (!options.hidden) glfwShowWindow(a.window);
}

App::~App() {
  Impl &a = *impl_;
  if (a.renderer) a.renderer->shutdown();
  a.renderer.reset();
  if (ImGui::GetCurrentContext()) {
    if (ImGui::GetIO().BackendPlatformUserData) ImGui_ImplGlfw_Shutdown();
    shutdown();
    ImGui::DestroyContext();
  }
  if (a.window) glfwDestroyWindow(a.window);
  if (a.glfw_ready) glfwTerminate();
  if (a.trace) std::fclose(a.trace);
}

bool App::ok() const { return impl_->window && impl_->renderer; }

int App::run(const std::function<void()> &frame) {
  Impl &a = *impl_;
  if (!ok()) return 1;
  a.frame = &frame;
  while (!glfwWindowShouldClose(a.window)) {
    // Render only while something changes: input, animation, layout settling.
    if (wants_frame() || a.extra_frames > 0) {
      glfwPollEvents();
    } else {
      const double timeout = idle_timeout();
      if (timeout < 0) glfwWaitEvents();
      else glfwWaitEventsTimeout(timeout);
      a.extra_frames = 1;
    }
    // Real input arrived (see the GLFW callbacks): ImGui settles hover and
    // active states one frame after the input, so render two frames.
    if (a.input_seen) a.extra_frames = 2;
    a.input_seen = false;
    if (a.trace) {
      std::fprintf(a.trace, "frame %d wants=%d extra=%d events=%d:", ImGui::GetFrameCount(), int(wants_frame()),
                   a.extra_frames, GImGui->InputEventsQueue.Size);
      for (const ImGuiInputEvent &e : GImGui->InputEventsQueue) std::fprintf(a.trace, " %d", int(e.Type));
      std::fprintf(a.trace, "\n");
      std::fflush(a.trace);
    }
    if (glfwGetWindowAttrib(a.window, GLFW_ICONIFIED)) {
      glfwWaitEvents();
      continue;
    }
    a.render_frame();
    if (a.extra_frames > 0) --a.extra_frames;
  }
  a.frame = nullptr;
  return 0;
}

void App::screenshot(const char *png_path) {
  impl_->screenshot_path = png_path ? png_path : "";
  request_frames(1);
}

void App::close() {
  if (impl_->window) glfwSetWindowShouldClose(impl_->window, GLFW_TRUE);
}

Backend App::backend() const { return impl_->backend; }

void App::set_backend(Backend backend) {
  if (backend == Backend::automatic) backend = available_backends().empty() ? backend : available_backends().front();
  impl_->wanted = backend;
  request_frames(2);
}

} // namespace silta
