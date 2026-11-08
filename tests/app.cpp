#include "silta/app.hpp"
#include "imgui.h"
#include <cmath>
#include <iostream>

int main() {
  // Wider than a hosted runner's desktop: Windows must not shrink a hidden
  // test surface to the monitor work area or change scripted hit targets.
  constexpr int width = 4096, height = 1024;
  silta::App app({.title = "Hidden-size regression", .width = width, .height = height,
                 .backend = silta::Backend::d3d11, .hidden = true});
  if (!app.ok()) return 1;
  bool correct = false;
  const int result = app.run([&] {
    const auto actual = ImGui::GetIO().DisplaySize;
    const float expected_width = width * silta::scale(), expected_height = height * silta::scale();
    correct = std::abs(actual.x - expected_width) <= 1 && std::abs(actual.y - expected_height) <= 1;
    if (!correct) std::cerr << "Expected hidden surface " << expected_width << 'x' << expected_height
                            << ", got " << actual.x << 'x' << actual.y << '\n';
    app.close();
  });
  return result ? result : correct ? 0 : 2;
}
