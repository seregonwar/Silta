#pragma once
// Android application shell for NativeActivity: window lifecycle (surface
// create/destroy, pause/resume), touch and key input through Dear ImGui's
// Android platform backend, the soft keyboard and a Vulkan renderer on the
// ANativeWindow.
//
//     #include <android_native_app_glue.h>
//     void android_main(android_app *app) {
//         silta::AppOptions options;
//         options.title = "My app";
//         silta::android_run(app, options, [&] { /* silta UI */ });
//     }
//
// android_run blocks until the activity is destroyed; it handles
// APP_CMD_INIT_WINDOW / TERM_WINDOW by rebuilding the renderer (UI state is
// kept) and only renders frames while Silta asks for them.
#include "silta/app.hpp"

struct android_app;

namespace silta {

// Runs the NativeActivity loop. `frame` is called once per rendered frame,
// like App::run's callback. Returns when the activity is destroyed.
void android_run(android_app *app, const AppOptions &options, const std::function<void()> &frame);

} // namespace silta
