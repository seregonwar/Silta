// iOS/iPadOS application shell: UIApplication (legacy window lifecycle), a
// full-window MTKView, touch input, the soft keyboard and the Metal renderer.
// The Info.plist must not declare a UIApplicationSceneManifest: the delegate
// uses the legacy window lifecycle. All state is file-scope: iOS has one
// window and one UI thread.
#include "silta/ios.hpp"
#include "imgui.h"
#include "imgui_internal.h"
#include "renderer.hpp"
#include <algorithm>
#include <cstdio>
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <MetalKit/MetalKit.h>
#import <QuartzCore/QuartzCore.h>
#import <UIKit/UIKit.h>

#if SILTA_HAS_METAL

namespace silta {

namespace {

struct Shell {
  const AppOptions *options = nullptr;
  const std::function<void()> *frame = nullptr;
  std::unique_ptr<detail::Renderer> renderer;
  bool renderer_ready = false;
  bool active = false;      // foreground; the display link draws only while active
  bool input_seen = false;  // a touch or keyboard event arrived since last frame
  int extra_frames = 2;
  MTKView *view = nil;      // the root view is the render surface
  UIWindow *window = nil;
  UITextField *keyboard = nil;
  CGRect keyboard_frame = CGRectZero; // keyboard in screen coordinates
  float keyboard_height = 0;          // on-screen keyboard overlap, in points
  double last_time = 0;
  id keyboard_observer = nil;
};

Shell shell;

// --------------------------------------------------------------------- input

// Touches emulate one mouse pointer (the primary finger); two-finger drags
// scroll. Gesture recognition beyond this is the touch-input roadmap item.
@interface SiltaTouchView : MTKView
@property(nonatomic, strong) UITouch *primary;
@property(nonatomic) CGPoint scroll_centroid;
@property(nonatomic) BOOL scrolling;
@property(nonatomic) NSInteger fingers;
- (CGPoint)centroid:(NSSet<UITouch *> *)touches;
@end

@implementation SiltaTouchView

- (void)touchesBegan:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
  (void)event;
  shell.input_seen = true;
  self.fingers += (NSInteger)touches.count;
  if (self.fingers >= 2) {
    if (!self.scrolling) mouse_button(false); // a tap that became a scroll
    self.scrolling = YES;
    self.scroll_centroid = [self centroid:event.allTouches];
    return;
  }
  UITouch *touch = touches.anyObject;
  if (!self.primary) self.primary = touch;
  ImGuiIO &io = ImGui::GetIO();
  const CGPoint p = touch_point(self.primary, self);
  io.AddMouseSourceEvent(ImGuiMouseSource_TouchScreen);
  io.AddMousePosEvent(float(p.x), float(p.y));
  io.AddMouseButtonEvent(0, true);
}

- (void)touchesMoved:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
  (void)event;
  shell.input_seen = true;
  ImGuiIO &io = ImGui::GetIO();
  if (self.scrolling) {
    const CGPoint c = [self centroid:event.allTouches];
    io.AddMouseWheelEvent(0, float(c.y - self.scroll_centroid.y) * 0.6f);
    self.scroll_centroid = c;
    return;
  }
  if (self.primary) {
    const CGPoint p = touch_point(self.primary, self);
    io.AddMouseSourceEvent(ImGuiMouseSource_TouchScreen);
    io.AddMousePosEvent(float(p.x), float(p.y));
  }
}

- (void)touchesEnded:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
  (void)event;
  shell.input_seen = true;
  self.fingers = std::max<NSInteger>(0, self.fingers - (NSInteger)touches.count);
  ImGuiIO &io = ImGui::GetIO();
  if (self.scrolling) {
    if (self.fingers == 0) self.scrolling = NO;
    return;
  }
  if (self.primary && [touches containsObject:self.primary]) {
    const CGPoint p = touch_point(self.primary, self);
    io.AddMouseSourceEvent(ImGuiMouseSource_TouchScreen);
    io.AddMousePosEvent(float(p.x), float(p.y));
    io.AddMouseButtonEvent(0, false);
    self.primary = nil;
  }
}

- (void)touchesCancelled:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
  shell.input_seen = true;
  self.fingers = std::max<NSInteger>(0, self.fingers - (NSInteger)touches.count);
  if (!self.scrolling && self.primary) {
    ImGuiIO &io = ImGui::GetIO();
    io.AddMouseButtonEvent(0, false);
    self.primary = nil;
  }
  if (self.fingers == 0) self.scrolling = NO;
}

- (CGPoint)centroid:(NSSet<UITouch *> *)touches {
  CGPoint sum = {0, 0};
  for (UITouch *touch in touches) {
    const CGPoint p = [touch locationInView:self];
    sum.x += p.x, sum.y += p.y;
  }
  if (touches.count) sum.x /= float(touches.count), sum.y /= float(touches.count);
  return sum;
}

@end

// The soft keyboard: an off-screen text field that becomes first responder
// while ImGui wants text, feeding characters and deletions back to ImGui.
@interface SiltaKeyboardView : UITextField <UITextFieldDelegate>
@end

@implementation SiltaKeyboardView

- (instancetype)initWithFrame:(CGRect)frame {
  if ((self = [super initWithFrame:frame])) {
    self.delegate = self;
    self.keyboardType = UIKeyboardTypeDefault;
    self.returnKeyType = UIReturnKeyDone;
    self.autocorrectionType = UITextAutocorrectionTypeNo;
    self.autocapitalizationType = UITextAutocapitalizationTypeNone;
    self.enablesReturnKeyAutomatically = NO;
    self.secureTextEntry = NO;
    self.textColor = [UIColor clearColor];
  }
  return self;
}

- (BOOL)textField:(UITextField *)field shouldChangeCharactersInRange:(NSRange)range
        replacementString:(NSString *)text {
  (void)field;
  ImGuiIO &io = ImGui::GetIO();
  if (text.length == 0) { // deletion
    for (NSUInteger i = 0; i < range.length && i < 64; ++i) {
      io.AddKeyEvent(ImGuiKey_Backspace, true);
      io.AddKeyEvent(ImGuiKey_Backspace, false);
    }
    return NO;
  }
  io.AddInputCharactersUTF8(text.UTF8String);
  return NO;
}

- (BOOL)textFieldShouldReturn:(UITextField *)field {
  (void)field;
  ImGuiIO &io = ImGui::GetIO();
  io.AddKeyEvent(ImGuiKey_Enter, true);
  io.AddKeyEvent(ImGuiKey_Enter, false);
  [self resignFirstResponder];
  return YES;
}

@end

// ------------------------------------------------------------------- frames

void update_display_size() {
  if (!shell.view) return;
  ImGuiIO &io = ImGui::GetIO();
  const CGRect bounds = shell.view.bounds;
  io.DisplaySize = ImVec2(float(bounds.size.width), float(bounds.size.height));
  const CGFloat scale = shell.view.contentScaleFactor > 0 ? shell.view.contentScaleFactor : 1;
  io.DisplayFramebufferScale = ImVec2(float(scale), float(scale));
}

// Keyboard overlap with the window, in points (0 when hidden). Called from the
// keyboard notification and after every layout change (rotation).
void update_keyboard_height() {
  if (!shell.view || !shell.window) return;
  const CGRect window_frame = shell.window.frame; // full-screen: screen coordinates
  const CGFloat overlap =
      std::max<CGFloat>(0, CGRectGetMaxY(window_frame) - shell.keyboard_frame.origin.y);
  shell.keyboard_height = float(overlap);
}

// The root window sits inside the safe areas (notch, home indicator, keyboard).
void draw_root_window() {
  const ImGuiViewport *vp = ImGui::GetMainViewport();
  const UIEdgeInsets insets = shell.view.safeAreaInsets;
  const float left = float(insets.left), top = float(insets.top);
  const float right = float(insets.right);
  const float bottom = float(insets.bottom) + shell.keyboard_height;
  ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + left, vp->WorkPos.y + top));
  ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x - left - right, vp->WorkSize.y - top - bottom));
  ImGui::Begin("##silta.root", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings |
                   ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus);
  if (shell.frame && *shell.frame) (*shell.frame)();
  ImGui::End();
}

void keyboard_follow_input_focus() {
  if (!shell.keyboard) return;
  const ImGuiIO &io = ImGui::GetIO();
  if (io.WantTextInput && !shell.keyboard.isFirstResponder) [shell.keyboard becomeFirstResponder];
  else if (!io.WantTextInput && shell.keyboard.isFirstResponder) [shell.keyboard resignFirstResponder];
}

void render_tick() {
  @autoreleasepool {
    if (!shell.active || !shell.renderer_ready || !shell.view) return;
    update_display_size();

    const double now = double(CACurrentMediaTime());
    ImGuiIO &io = ImGui::GetIO();
    io.DeltaTime = float(shell.last_time ? std::min(0.1, std::max(0.001, now - shell.last_time))
                                         : 1.0 / 60.0);
    shell.last_time = now;

    keyboard_follow_input_focus();
    if (shell.input_seen) shell.extra_frames = 2;
    shell.input_seen = false;
    if (!silta::wants_frame() && shell.extra_frames <= 0) return;

    shell.renderer->new_frame();
    ImGui::NewFrame();
    begin_frame();
    draw_root_window();
    end_frame();
    ImGui::Render();
    shell.renderer->render(ImGui::GetDrawData(), theme().background);
    if (shell.extra_frames > 0) --shell.extra_frames;
  }
}

// ------------------------------------------------------------------- launch

void start_ui() {
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO &io = ImGui::GetIO();
  io.IniFilename = nullptr;
  io.ConfigMacOSXBehaviors = false; // no command key on iOS

  InitOptions init_options = shell.options->init;
  init_options.style_imgui = true; // the shell's windows are Silta's own
  init_options.scale = 1;          // iOS points are already device-independent
  init(init_options);

  MTKView *view = (MTKView *)shell.view;
  if (!view) return;
  view.device = MTLCreateSystemDefaultDevice();
  if (!view.device) {
    std::fprintf(stderr, "silta: no Metal device is available\n");
    return;
  }
  view.paused = YES; // the display link drives the frames
  view.enableSetNeedsDisplay = NO;
  view.delegate = nil;
  view.framebufferOnly = NO; // screenshots may read the frame back
  view.colorPixelFormat = MTLPixelFormatBGRA8Unorm;
  view.depthStencilPixelFormat = MTLPixelFormatInvalid;
  view.sampleCount = 1;
  view.clearColor = MTLClearColorMake(0, 0, 0, 1);

  shell.renderer = detail::make_metal_renderer();
  if (!shell.renderer->init_native((__bridge void *)view, int(view.drawableSize.width),
                                   int(view.drawableSize.height))) {
    shell.renderer.reset();
    return;
  }
  shell.renderer_ready = true;

  shell.keyboard = [[SiltaKeyboardView alloc] initWithFrame:CGRectMake(0, -100, 1, 1)];
  [shell.view addSubview:shell.keyboard];

  __weak UIWindow *weak_window = shell.window;
  shell.keyboard_observer = [[NSNotificationCenter defaultCenter]
      addObserverForName:UIKeyboardDidChangeFrameNotification
                  object:nil
                   queue:nil
              usingBlock:^(NSNotification *note) {
                Shell &s = shell; // one UI thread
                CGRect frame = CGRectZero;
                if (UIWindow *window = weak_window) {
                  id value = note.userInfo[UIKeyboardFrameEndUserInfoKey];
                  if (value) frame = [value CGRectValue];
                  s.keyboard_frame = frame;
                  update_keyboard_height();
                }
              }];
}

void teardown() {
  if (shell.keyboard_observer) {
    [[NSNotificationCenter defaultCenter] removeObserver:shell.keyboard_observer];
    shell.keyboard_observer = nil;
  }
  shell.renderer.reset(); // ImGui's Metal backend shuts down with it
  if (ImGui::GetCurrentContext()) {
    shutdown();
    ImGui::DestroyContext();
  }
  shell.renderer_ready = false;
}

// ------------------------------------------------------------- Cocoa objects

@interface SiltaDisplayTarget : NSObject
- (void)display_tick:(CADisplayLink *)link;
@end

@implementation SiltaDisplayTarget
- (void)display_tick:(CADisplayLink *)link {
  (void)link;
  render_tick();
}
@end

@interface SiltaViewController : UIViewController
@end

@implementation SiltaViewController
- (void)loadView {
  shell.view = [[SiltaTouchView alloc] initWithFrame:UIScreen.mainScreen.bounds device:nil];
  self.view = shell.view;
}

- (void)viewDidLayoutSubviews {
  [super viewDidLayoutSubviews];
  update_keyboard_height();
}
@end

@interface SiltaAppDelegate : NSObject <UIApplicationDelegate>
@property(nonatomic, strong) UIWindow *window; // legacy lifecycle: UIApplication reads this
@property(nonatomic, strong) CADisplayLink *link;
@end

@implementation SiltaAppDelegate

- (BOOL)application:(UIApplication *)application didFinishLaunchingWithOptions:(NSDictionary *)options {
  (void)application, (void)options;
  if (!shell.options || !shell.frame || !*shell.frame) return NO;

  UIWindow *window = [[UIWindow alloc] initWithFrame:UIScreen.mainScreen.bounds];
  window.rootViewController = [SiltaViewController new];
  window.backgroundColor = UIColor.systemBackgroundColor;
  [window makeKeyAndVisible];
  self.window = window;
  shell.window = window;

  start_ui();
  if (!shell.renderer_ready) {
    std::fprintf(stderr, "silta: the Metal renderer could not start\n");
    return NO;
  }
  self.link = [CADisplayLink displayLinkWithTarget:[SiltaDisplayTarget new]
                                          selector:@selector(display_tick:)];
  self.link.preferredFramesPerSecond = 0; // the display's natural rate
  [self.link addToRunLoop:[NSRunLoop mainRunLoop] forMode:NSRunLoopCommonModes];
  return YES;
}

- (void)applicationDidBecomeActive:(UIApplication *)application {
  (void)application;
  shell.active = true;
  shell.last_time = 0;
}

- (void)applicationWillResignActive:(UIApplication *)application {
  (void)application;
  shell.active = false;
}

- (void)applicationWillTerminate:(UIApplication *)application {
  (void)application;
  teardown();
}

- (UIInterfaceOrientationMask)application:(UIApplication *)application
    supportedInterfaceOrientationsForWindow:(UIWindow *)window {
  (void)application, (void)window;
  return UIInterfaceOrientationMaskAllButUpsideDown;
}

@end

} // namespace silta

int ios_main(int argc, char **argv, const AppOptions &options, const std::function<void()> &frame) {
  shell.options = &options;
  shell.frame = &frame;
  return UIApplicationMain(argc, argv, nil, NSStringFromClass([SiltaAppDelegate class]));
}

#else // !SILTA_HAS_METAL

int ios_main(int, char **, const AppOptions &, const std::function<void()> &) {
  std::fprintf(stderr, "silta: ios_main needs a build with the Metal renderer\n");
  return 1;
}

#endif
