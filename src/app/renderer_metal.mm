// Metal renderer, based on Dear ImGui's example_apple_metal (MIT).
// Serves the macOS window shell through the window's content view, and mobile
// shells through init_native() with an MTKView the host owns.
#include "renderer.hpp"
#include "imgui.h"
#include "imgui_impl_metal.h"
#import <Metal/Metal.h>
#import <MetalKit/MetalKit.h>
#import <QuartzCore/QuartzCore.h>
#import <TargetConditionals.h>

#if TARGET_OS_MAC && !TARGET_OS_IPHONE
#define SILTA_METAL_WINDOW
#endif

#include <cstdio>
#include <cstring>

#ifdef SILTA_METAL_WINDOW
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3native.h>
#import <AppKit/AppKit.h>
#endif

namespace silta::detail {
namespace {

class MetalRenderer final : public Renderer {
public:
  ~MetalRenderer() override { shutdown(); }

#ifndef SILTA_METAL_WINDOW
  // Mobile shells never hand a GLFW window over.
  bool init(GLFWwindow *) override { return false; }
#endif

  // ------------------------------------------------------------- window shell
#ifdef SILTA_METAL_WINDOW
  bool init(GLFWwindow *window) override {
    NSWindow *ns_window = glfwGetCocoaWindow(window);
    if (!ns_window || !ns_window.contentView) {
      std::fprintf(stderr, "silta: Metal cannot reach the window's content view\n");
      return false;
    }
    view_ = [[MTKView alloc] initWithFrame:ns_window.contentView.bounds
                                    device:MTLCreateSystemDefaultDevice()];
    if (!view_ || !view_.device) {
      std::fprintf(stderr, "silta: no Metal device is available\n");
      view_ = nil;
      return false;
    }
    owns_view_ = true;
    configure_view();
    view_.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    [ns_window.contentView addSubview:view_];
    return start_backend();
  }
#endif

  // ------------------------------------------------------- native mobile host
  bool init_native(void *handle, int width, int height) override {
    (void)width, (void)height;
    view_ = (__bridge MTKView *)handle;
    if (!view_ || ![view_ isKindOfClass:[MTKView class]]) {
      std::fprintf(stderr, "silta: Metal needs an MTKView as its native surface\n");
      view_ = nil;
      return false;
    }
    if (!view_.device) view_.device = MTLCreateSystemDefaultDevice();
    if (!view_.device) {
      std::fprintf(stderr, "silta: no Metal device is available\n");
      view_ = nil;
      return false;
    }
    owns_view_ = false;
    configure_view();
    return start_backend();
  }

  void new_frame() override {
    if (!ready()) return;
    MTLRenderPassDescriptor *pass = view_.currentRenderPassDescriptor; // acquires a drawable
    if (!pass) return;
    ImGui_ImplMetal_NewFrame(pass);
  }

  void render(ImDrawData *data, Color clear, Capture *capture) override {
    if (!ready() || !data || data->DisplaySize.x <= 0 || data->DisplaySize.y <= 0) return;
    id<CAMetalDrawable> drawable = view_.currentDrawable;
    MTLRenderPassDescriptor *pass = view_.currentRenderPassDescriptor;
    if (!drawable || !pass) return; // drawable pool busy; the caller asks for another frame

    const MTLClearColor color = MTLClearColorMake(clear.r * clear.a, clear.g * clear.a, clear.b * clear.a, clear.a);
    pass.colorAttachments[0].clearColor = color;
    pass.colorAttachments[0].loadAction = MTLLoadActionClear;
    pass.colorAttachments[0].storeAction = MTLStoreActionStore;

    id<MTLCommandBuffer> buffer = [queue_ commandBuffer];
    id<MTLRenderCommandEncoder> encoder = [buffer renderCommandEncoderWithDescriptor:pass];
    ImGui_ImplMetal_RenderDrawData(data, buffer, encoder);
    [encoder endEncoding];

    MTLSize pixels = MTLSizeMake(drawable.texture.width, drawable.texture.height, 1);
    Readback readback;
    if (capture) readback = start_read_back(buffer, drawable.texture, pixels);

    [buffer presentDrawable:drawable];
    [buffer commit];
    if (capture && readback.buffer) {
      [buffer waitUntilCompleted];
      finish_read_back(readback, pixels, *capture);
    }
  }

  void shutdown() override {
    if (backend_ready_) ImGui_ImplMetal_Shutdown();
    backend_ready_ = false;
    if (owns_view_) [view_ removeFromSuperview]; // the window keeps another renderer's surface
    view_ = nil;                                 // a host-owned view stays with the host
    queue_ = nil;
    device_ = nil;
  }

private:
  struct Readback {
    id<MTLBuffer> buffer;
  };

  bool ready() const { return view_ && queue_ && backend_ready_; }

  void configure_view() {
    view_.paused = YES;          // the shell drives the frames
    view_.enableSetNeedsDisplay = NO;
    view_.delegate = nil;
    view_.framebufferOnly = NO;  // screenshots read the frame back
    view_.colorPixelFormat = MTLPixelFormatBGRA8Unorm;
    view_.depthStencilPixelFormat = MTLPixelFormatInvalid;
    view_.sampleCount = 1;
    view_.clearColor = MTLClearColorMake(0, 0, 0, 1);
    device_ = view_.device;
  }

  bool start_backend() {
    queue_ = [view_.device newCommandQueue];
    if (!queue_) {
      std::fprintf(stderr, "silta: could not create a Metal command queue\n");
      return false;
    }
    if (!ImGui_ImplMetal_Init(view_.device)) {
      std::fprintf(stderr, "silta: ImGui's Metal backend failed to start\n");
      return false;
    }
    backend_ready_ = true;
    return true;
  }

  Readback start_read_back(id<MTLCommandBuffer> buffer, id<MTLTexture> texture, MTLSize pixels) {
    const NSUInteger row_bytes = pixels.width * 4;
    id<MTLBuffer> staging = [device_ newBufferWithLength:row_bytes * pixels.height
                                                 options:MTLResourceStorageModeShared];
    if (!staging) return {};
    id<MTLBlitCommandEncoder> blit = [buffer blitCommandEncoder];
    [blit copyFromTexture:texture
              sourceSlice:0
              sourceLevel:0
             sourceOrigin:MTLOriginMake(0, 0, 0)
               sourceSize:pixels
                 toBuffer:staging
         destinationOffset:0
        destinationBytesPerRow:row_bytes
      destinationBytesPerImage:pixels.height * 4];
    [blit endEncoding];
    return {staging};
  }

  void finish_read_back(Readback &readback, MTLSize pixels, Capture &out) {
    const NSUInteger width = pixels.width, height = pixels.height;
    out.width = int(width), out.height = int(height);
    out.rgba.resize(size_t(width) * height * 4);
    const uint8_t *src = static_cast<const uint8_t *>(readback.buffer.contents);
    for (size_t i = 0; i < out.rgba.size(); i += 4) {
      out.rgba[i + 0] = src[i + 2]; // BGRA drawable pixels -> RGBA
      out.rgba[i + 1] = src[i + 1];
      out.rgba[i + 2] = src[i + 0];
      out.rgba[i + 3] = 255;
    }
  }

  id<MTLDevice> device_; // from the view; kept for read-back allocation
  MTKView *view_ = nil;
  id<MTLCommandQueue> queue_ = nil;
  bool owns_view_ = false;
  bool backend_ready_ = false;
};

} // namespace

std::unique_ptr<Renderer> make_metal_renderer() { return std::make_unique<MetalRenderer>(); }

} // namespace silta::detail
