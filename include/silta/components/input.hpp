#pragma once
#include <cstddef>
#include <string>
#include "silta/layout.hpp"

namespace silta {

struct InputOptions {
  const char *placeholder = nullptr;
  float width = fill; // points, or fill (CSS w-full, the shadcn default)
  bool disabled = false;
  bool password = false;
  ImGuiInputTextFlags flags = 0; // extra ImGui flags
};

// Single-line text field. Returns true when the text changed.
bool Input(const char *id, char *buffer, std::size_t buffer_size, const InputOptions &options = {});
bool Input(const char *id, std::string *text, const InputOptions &options = {});

} // namespace silta
