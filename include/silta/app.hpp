#pragma once
// Optional application shell: a window, a render loop that sleeps when nothing
// moves, and interchangeable renderer backends (switchable at runtime).
//
//   silta::App app({.title = "My app"});
//   return app.run([&] { /* silta UI */ });
//
// Apps with their own renderer (an engine, a video player) do not need this:
// they call silta::begin_frame()/end_frame() inside their own ImGui frame.
#include <functional>
#include <memory>
#include <vector>
#include "silta/context.hpp"

namespace silta {

enum class Backend { automatic, d3d11, vulkan };

const char *backend_name(Backend backend);
// Backends compiled into this build, best first.
std::vector<Backend> available_backends();

struct AppOptions {
  const char *title = "Silta";
  int width = 1280; // points
  int height = 800; // points
  Backend backend = Backend::automatic;
  InitOptions init;
  // Draw the UI in a full-window, scrollable root with the theme background.
  bool root_window = true;
  // Never show the window (automated tests and screenshots).
  bool hidden = false;
};

class App {
public:
  explicit App(const AppOptions &options = {});
  ~App();
  App(const App &) = delete;
  App &operator=(const App &) = delete;

  // False when no window or renderer could be created.
  bool ok() const;
  // Calls `frame` once per rendered frame until the window closes.
  int run(const std::function<void()> &frame);

  Backend backend() const;
  // Swaps the renderer at the start of the next frame. The UI state is kept.
  void set_backend(Backend backend);

  // Saves the next rendered frame as a PNG, read back from the GPU (works for
  // hidden windows too: the base of visual regression tests).
  void screenshot(const char *png_path);
  // Ends run() after the current frame.
  void close();

  struct Impl;

private:
  std::unique_ptr<Impl> impl_;
};

} // namespace silta
