// Android application shell for NativeActivity: window, lifecycle and input
// through the NDK's android_native_app_glue (input events reach onInputEvent
// on the UI thread), touch and key input through Dear ImGui's Android
// platform backend, the soft keyboard, and the Vulkan renderer on the
// ANativeWindow. One instance per process, one UI thread.
#include "silta/android.hpp"
#include "imgui.h"
#include "imgui_impl_android.h"
#include "renderer.hpp"
#include <algorithm>
#include <android/input.h>
#include <android/keycodes.h>
#include <android/log.h>
#include <android/native_activity.h>
#include <android_native_app_glue.h>
#include <chrono>
#include <cstdio>

#if SILTA_HAS_VULKAN

namespace silta {

namespace {

struct Shell {
  const AppOptions *options = nullptr;
  const std::function<void()> *frame = nullptr;
  std::unique_ptr<detail::Renderer> renderer;
  bool renderer_ready = false;
  bool paused = true;
  bool input_seen = false;
  int extra_frames = 2;
  ARect content_rect = {0, 0, 0, 0};
  float scale = 1;
  double frame_interval = 1.0 / 60.0;
  double last_time = 0;
  std::chrono::steady_clock::time_point next_frame;
  jmethodID unicode_of = nullptr;
  jclass key_event_class = nullptr;
  bool keyboard_shown = false;
};

Shell shell;

void log(const char *what) { __android_log_print(ANDROID_LOG_WARN, "silta", "%s", what); }

// --------------------------------------------------------------- JNI helpers

// ImGui's Android backend maps key codes but does not feed characters
// (issue #3446); android.view.KeyEvent supplies the Unicode value.
void feed_unicode(android_app *app, AInputEvent *event) {
  JNIEnv *env = app->activity ? app->activity->env : nullptr;
  if (!env) return;
  if (!shell.unicode_of) {
    jclass local = env->FindClass("android/view/KeyEvent");
    if (!local || env->ExceptionCheck()) {
      env->ExceptionClear();
      return;
    }
    shell.unicode_of = env->GetStaticMethodID(local, "getUnicodeChar", "(II)I");
    if (env->ExceptionCheck()) {
      env->ExceptionClear();
      shell.unicode_of = nullptr;
      env->DeleteLocalRef(local);
      return;
    }
    shell.key_event_class = (jclass)env->NewGlobalRef(local);
    env->DeleteLocalRef(local);
  }
  const int32_t code = AKeyEvent_getKeyCode(event);
  const int32_t meta = AKeyEvent_getMetaState(event);
  const jint unicode = env->CallStaticIntMethod(shell.key_event_class, shell.unicode_of, code, meta);
  if (env->ExceptionCheck()) {
    env->ExceptionClear();
    return;
  }
  if (unicode > 0) ImGui::GetIO().AddInputCharacter(unsigned(unicode));
}

void keyboard_follow_input_focus(android_app *app) {
  ANativeActivity *activity = app->activity;
  if (!activity) return;
  const bool want = ImGui::GetIO().WantTextInput;
  if (want == shell.keyboard_shown) return;
  if (want) ANativeActivity_showSoftInput(activity, ANATIVEACTIVITY_SHOW_SOFT_INPUT_IMPLICIT);
  else ANativeActivity_hideSoftInput(activity, ANATIVEACTIVITY_HIDE_SOFT_INPUT_NOT_ALWAYS);
  shell.keyboard_shown = want;
}

// -------------------------------------------------------------- window state

void query_scale(android_app *app) {
  if (app->config) {
    const int32_t density = AConfiguration_getDensity(app->config);
    if (density > 0) shell.scale = float(density) / 160.f; // 160 dpi = 1 CSS pixel
  }
}

void start_renderer(android_app *app) {
  if (shell.renderer_ready || !app->window) return;
  IMGUI_CHECKVERSION();
  IM_ASSERT(ImGui::GetCurrentContext() && "the ImGui context is created by android_run");
  ImGui_ImplAndroid_Init(app->window);
  query_scale(app);
  set_scale(shell.scale);
  const int width = ANativeWindow_getWidth(app->window);
  const int height = ANativeWindow_getHeight(app->window);
  shell.renderer = detail::make_vulkan_renderer();
  if (!shell.renderer->init_native((void *)app->window, width, height)) {
    shell.renderer.reset();
    ImGui_ImplAndroid_Shutdown();
    log("the Vulkan renderer could not start");
    return;
  }
  shell.renderer_ready = true;
  shell.extra_frames = 2;
  shell.next_frame = std::chrono::steady_clock::now();
}

void stop_renderer() {
  if (!shell.renderer_ready) return;
  ImGui_ImplAndroid_Shutdown();
  shell.renderer.reset(); // destroys the Vulkan surface (releases ANativeWindow)
  shell.renderer_ready = false;
}

void render_frame(android_app *app) {
  ImGui_ImplAndroid_NewFrame();
  shell.renderer->new_frame();
  const int window_w = app->window ? ANativeWindow_getWidth(app->window) : 0;
  const int window_h = app->window ? ANativeWindow_getHeight(app->window) : 0;
  shell.renderer->resize(window_w, window_h);

  ImGui::NewFrame();
  begin_frame();
  // The root window sits inside the content rect (nav bar, display cutout).
  const ImGuiViewport *vp = ImGui::GetMainViewport();
  const float left = float(shell.content_rect.left), top = float(shell.content_rect.top);
  const float right = float(window_w - shell.content_rect.right);
  const float bottom = float(window_h - shell.content_rect.bottom);
  ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + left, vp->WorkPos.y + top));
  ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x - left - right, vp->WorkSize.y - top - bottom));
  ImGui::Begin("##silta.root", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings |
                   ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus);
  if (shell.frame && *shell.frame) (*shell.frame)();
  ImGui::End();
  end_frame();
  ImGui::Render();
  shell.renderer->render(ImGui::GetDrawData(), theme().background);
  keyboard_follow_input_focus(app);
  if (shell.extra_frames > 0) --shell.extra_frames;
}

// ------------------------------------------------------------- glue callbacks

void handle_cmd(android_app *app, int32_t cmd) {
  switch (cmd) {
  case APP_CMD_INIT_WINDOW: start_renderer(app); break;
  case APP_CMD_TERM_WINDOW: stop_renderer(); break;
  case APP_CMD_RESUME:
    shell.paused = false;
    shell.next_frame = std::chrono::steady_clock::now();
    break;
  case APP_CMD_PAUSE: shell.paused = true; break;
  case APP_CMD_CONTENT_RECT_CHANGED:
  case APP_CMD_CONFIG_CHANGED:
    if (app->window) shell.content_rect = app->contentRect;
    query_scale(app);
    set_scale(shell.scale);
    shell.extra_frames = std::max(shell.extra_frames, 2);
    break;
  default: break;
  }
}

// The glue delivers each input event here, on the UI thread. Returns 1 when
// the event was consumed.
int32_t handle_input(android_app *app, AInputEvent *event) {
  if (!ImGui::GetCurrentContext()) return 0;
  if (AInputEvent_getType(event) == AINPUT_EVENT_TYPE_KEY) {
    const int32_t action = AKeyEvent_getAction(event);
    if (AKeyEvent_getKeyCode(event) == AKEYCODE_BACK) {
      // The UI decides; Escape reaches the frontend as "go back".
      if (action == AKEY_EVENT_ACTION_DOWN) {
        ImGuiIO &io = ImGui::GetIO();
        io.AddKeyEvent(ImGuiKey_Escape, true);
        io.AddKeyEvent(ImGuiKey_Escape, false);
        shell.input_seen = true;
      }
      return 1;
    }
    const int32_t handled = ImGui_ImplAndroid_HandleInputEvent(event);
    if (action == AKEY_EVENT_ACTION_DOWN) feed_unicode(app, event);
    shell.input_seen = true;
    return handled;
  }
  shell.input_seen = true;
  return ImGui_ImplAndroid_HandleInputEvent(event);
}

// -------------------------------------------------------------- the main loop

// The current time in seconds since an arbitrary steady epoch.
double steady_seconds(const std::chrono::steady_clock::time_point &t) {
  return std::chrono::duration<double>(t.time_since_epoch()).count();
}

// Milliseconds the loop may block before the next due frame, or -1 forever.
int poll_timeout() {
  using clock = std::chrono::steady_clock;
  if (shell.paused || !shell.renderer_ready) return -1; // wait for lifecycle events
  const clock::time_point now = clock::now();
  if (shell.next_frame > now)
    return int(std::chrono::duration_cast<std::chrono::milliseconds>(shell.next_frame - now).count());
  return 0; // a frame is due now
}

} // namespace

void android_run(android_app *app, const AppOptions &options, const std::function<void()> &frame) {
  if (!app || !frame) return;
  shell.options = &options;
  shell.frame = &frame;
  shell.paused = true;

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO &io = ImGui::GetIO();
  io.IniFilename = nullptr;
  InitOptions init_options = options.init;
  init_options.style_imgui = true; // the shell's windows are Silta's own
  init(init_options);

  app->onAppCmd = handle_cmd;
  app->onInputEvent = handle_input;

  using clock = std::chrono::steady_clock;
  while (!app->destroyRequested) {
    int events = 0;
    android_poll_source *source = nullptr;
    const int ident = ALooper_pollOnce(poll_timeout(), nullptr, &events, (void **)&source);
    if (ident >= 0 && source) source->process(app, source); // lifecycle or input

    const clock::time_point now = clock::now();
    const double now_s = steady_seconds(now);
    io.DeltaTime = float(shell.last_time ? std::min(0.1, std::max(0.001, now_s - shell.last_time))
                                         : 1.0 / 60.0);
    shell.last_time = now_s;

    if (shell.renderer_ready && !shell.paused && now >= shell.next_frame &&
        (shell.input_seen || silta::wants_frame() || shell.extra_frames > 0)) {
      render_frame(app);
      shell.input_seen = false;
      shell.next_frame =
          now + std::chrono::duration_cast<clock::duration>(std::chrono::duration<double>(shell.frame_interval));
    } else if (shell.next_frame < now) {
      shell.next_frame = now; // catch up after idle sleeps
    }
  }

  stop_renderer();
  shell.frame = nullptr;
  shutdown();
  ImGui::DestroyContext();
}

} // namespace silta

#else // !SILTA_HAS_VULKAN

namespace silta {
void android_run(android_app *app, const AppOptions &, const std::function<void()> &) {
  (void)app;
  __android_log_print(ANDROID_LOG_WARN, "silta", "android_run needs a build with the Vulkan renderer");
}
} // namespace silta

#endif
