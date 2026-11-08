#pragma once
// Font files and the platform's fallback fonts (internal).
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace silta::detail {

// A read-only file mapped into memory: font collections can be tens of
// megabytes, and only the pages the rasterizer reads become resident.
class MappedFile {
public:
  static std::unique_ptr<MappedFile> open(const std::string &utf8_path);
  ~MappedFile();
  const unsigned char *data() const { return data_; }
  std::size_t size() const { return size_; }

private:
  MappedFile() = default;
  const unsigned char *data_ = nullptr;
  std::size_t size_ = 0;
  void *file_ = nullptr, *mapping_ = nullptr; // platform handles
};

struct SystemFont {
  std::string path; // UTF-8
  int index = 0;    // face in a collection
  bool color = false; // colour glyphs (emoji)
};

// The platform's fonts for scripts Inter does not cover, best first, with the
// CJK fonts of the user's language ahead of the others. Only existing files.
std::vector<SystemFont> system_fallback_fonts();

} // namespace silta::detail
