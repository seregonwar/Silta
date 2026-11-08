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

struct TextareaOptions {
  const char *placeholder = nullptr;
  float width = fill;        // points, or fill
  float min_height = 64;     // points (min-h-16)
  float max_height = 240;    // points; it grows with its text up to here, then scrolls
  bool disabled = false;
  bool wrap = true;          // wrap long lines (off: scroll sideways)
  ImGuiInputTextFlags flags = 0;
};

// Multi-line text field that grows with its content (field-sizing: content).
// Returns true when the text changed.
bool Textarea(const char *id, std::string *text, const TextareaOptions &options = {});

} // namespace silta
