#include "silta/texture.hpp"
#include "silta/context.hpp"
#include "silta/draw.hpp"
#include "per_context.hpp"
#include "imgui_internal.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace silta {
namespace {
// Textures waiting for the backend to release their GPU side, per context.
struct Graveyard {
  std::vector<ImTextureData *> textures;
  // The context is going away: its backend has released everything already.
  ~Graveyard() {
    for (ImTextureData *tex : textures) IM_DELETE(tex);
  }
};

void retire(ImTextureData *tex, ImGuiContext *context) {
  if (!tex) return;
  if (!detail::context_alive(context)) { // context gone: so is its backend
    IM_DELETE(tex);
    return;
  }
  tex->WantDestroyNextFrame = true;
  detail::per_context<Graveyard>(context).textures.push_back(tex);
}
} // namespace

namespace detail {
void collect_textures() {
  auto &graveyard = per_context<Graveyard>().textures;
  for (auto it = graveyard.begin(); it != graveyard.end();) {
    ImTextureData *tex = *it;
    if (tex->Status == ImTextureStatus_Destroyed) {
      ImGui::UnregisterUserTexture(tex);
      IM_DELETE(tex);
      it = graveyard.erase(it);
    } else {
      ++it;
    }
  }
  if (!graveyard.empty()) request_frames(1); // let the backend finish
}
} // namespace detail

Texture::~Texture() { reset(); }

Texture::Texture(Texture &&other) noexcept : tex_(other.tex_), context_(other.context_) {
  other.tex_ = nullptr;
  other.context_ = nullptr;
}

Texture &Texture::operator=(Texture &&other) noexcept {
  if (this != &other) {
    reset();
    tex_ = other.tex_;
    context_ = other.context_;
    other.tex_ = nullptr;
    other.context_ = nullptr;
  }
  return *this;
}

void Texture::reset() {
  retire(tex_, context_);
  tex_ = nullptr;
  context_ = nullptr;
}

int Texture::width() const { return tex_ ? tex_->Width : 0; }
int Texture::height() const { return tex_ ? tex_->Height : 0; }
ImTextureRef Texture::ref() const { return tex_ ? tex_->GetTexRef() : ImTextureRef(); }

void Texture::update(const void *rgba, int width, int height, int stride) {
  if (!rgba || width <= 0 || height <= 0) return;
  if (stride <= 0) stride = width * 4;
  const bool fresh = !tex_ || tex_->Width != width || tex_->Height != height;
  if (fresh) {
    reset();
    tex_ = IM_NEW(ImTextureData)();
    tex_->Create(ImTextureFormat_RGBA32, width, height);
    tex_->UseColors = true;
    ImGui::RegisterUserTexture(tex_);
    context_ = ImGui::GetCurrentContext();
  }
  const auto *src = static_cast<const unsigned char *>(rgba);
  for (int y = 0; y < height; ++y)
    std::memcpy(tex_->GetPixelsAt(0, y), src + size_t(y) * size_t(stride), size_t(width) * 4);
  if (!fresh && tex_->Status == ImTextureStatus_OK) {
    const ImTextureRect all = {0, 0, static_cast<unsigned short>(width), static_cast<unsigned short>(height)};
    tex_->Updates.resize(0);
    tex_->Updates.push_back(all);
    tex_->UpdateRect = all;
    tex_->UsedRect = all;
    tex_->SetStatus(ImTextureStatus_WantUpdates);
  }
  request_frames(1);
}

ImRect draw_image(ImDrawList *list, const Texture &texture, const ImRect &box, Fit fit, float radius_px, Color tint) {
  if (!texture.valid() || box.GetWidth() <= 0 || box.GetHeight() <= 0) return box;
  const float tw = float(texture.width()), th = float(texture.height());
  ImRect dst = box;
  ImVec2 uv0(0, 0), uv1(1, 1);
  const float box_aspect = box.GetWidth() / box.GetHeight(), tex_aspect = tw / th;
  if (fit == Fit::contain) {
    if (tex_aspect > box_aspect) {
      const float h = box.GetWidth() / tex_aspect, y = box.Min.y + (box.GetHeight() - h) / 2;
      dst = ImRect(ImVec2(box.Min.x, y), ImVec2(box.Max.x, y + h));
    } else {
      const float w = box.GetHeight() * tex_aspect, x = box.Min.x + (box.GetWidth() - w) / 2;
      dst = ImRect(ImVec2(x, box.Min.y), ImVec2(x + w, box.Max.y));
    }
  } else if (fit == Fit::cover) {
    if (tex_aspect > box_aspect) {
      const float visible = box_aspect / tex_aspect; // fraction of the width shown
      uv0.x = (1 - visible) / 2, uv1.x = 1 - uv0.x;
    } else {
      const float visible = tex_aspect / box_aspect;
      uv0.y = (1 - visible) / 2, uv1.y = 1 - uv0.y;
    }
  }
  dst = ImRect(ImVec2(std::round(dst.Min.x), std::round(dst.Min.y)), ImVec2(std::round(dst.Max.x), std::round(dst.Max.y)));
  if (radius_px > 0) list->AddImageRounded(texture.ref(), dst.Min, dst.Max, uv0, uv1, tint.u32(), radius_px);
  else list->AddImage(texture.ref(), dst.Min, dst.Max, uv0, uv1, tint.u32());
  return dst;
}

ImRect Image(const Texture &texture, const ImageOptions &o) {
  const float aspect = texture.valid() ? float(texture.width()) / float(texture.height()) : 16.f / 9.f;
  const float natural_w = texture.valid() ? float(texture.width()) : px(160);
  const float w = layout::resolve_width(o.width, natural_w);
  const float h = layout::resolve_height(o.height, w / aspect);
  const ImRect box = layout::allocate(ImVec2(w, h), o.width == fill);
  ImDrawList *dl = ImGui::GetWindowDrawList();
  if (o.background.a > 0) draw::fill(dl, box, o.background, px(o.radius));
  return draw_image(dl, texture, box, o.fit, px(o.radius));
}

} // namespace silta
