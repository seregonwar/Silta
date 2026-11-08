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

// A font face loaded by the current context (bytes owned by the context).
struct FaceData {
  const unsigned char *data = nullptr;
  std::size_t size = 0;
  int index = 0;
  bool color = false;
};
// Inter for weight 0 (regular), 1 (medium) or 2 (semibold), and the fallback
// chain shared by all weights, in lookup order. Used by the text shaper.
const FaceData &inter_face(int weight);
const std::vector<FaceData> &fallback_faces();

} // namespace silta::detail
