#pragma once
// GPU textures from CPU pixels, on any renderer backend.
//
// Built on Dear ImGui 1.92's user textures: Silta hands the pixels to ImGui
// and the active backend (D3D11, Vulkan, Metal, ...) creates, updates and
// destroys the GPU texture. Updating every frame (video) is supported.
#include <cstdint>
#include "silta/color.hpp"
#include "silta/layout.hpp"
#include "imgui.h"

struct ImTextureData;

namespace silta {

class Texture {
public:
  Texture() = default;
  ~Texture();
  Texture(Texture &&other) noexcept;
  Texture &operator=(Texture &&other) noexcept;
  Texture(const Texture &) = delete;
  Texture &operator=(const Texture &) = delete;

  // Replaces the pixels: RGBA8, top-down rows. `stride` in bytes (0 = width*4).
  // A new size recreates the texture.
  void update(const void *rgba, int width, int height, int stride = 0);
  void reset();

  bool valid() const { return tex_ != nullptr; }
  int width() const;
  int height() const;
  ImTextureRef ref() const;

private:
  ImTextureData *tex_ = nullptr;
};

enum class Fit { contain, cover, fill };

// Draws a texture into `box` (pixels). Returns the rectangle the picture
// covers (for contain, it can be smaller than the box).
ImRect draw_image(ImDrawList *list, const Texture &texture, const ImRect &box, Fit fit = Fit::contain,
                  float radius_px = 0, Color tint = white);

struct ImageOptions {
  float width = fill;  // points, fill, or 0 (natural size)
  float height = 0;    // points, fill, or 0 (from the aspect ratio)
  Fit fit = Fit::contain;
  float radius = 0;    // points
  Color background = transparent;
};
// A picture placed by the layout. Returns the rectangle of the picture.
ImRect Image(const Texture &texture, const ImageOptions &options = {});

namespace detail {
// Frees textures the backend has released (called by silta::begin_frame).
void collect_textures();
} // namespace detail

} // namespace silta
