#pragma once
// iOS and iPadOS application shell: a UIApplication that owns the window
// (UIWindow + MTKView), touch and Apple Pencil input, the soft keyboard and a
// Metal renderer.
//
//     int main(int argc, char **argv) {
//         silta::AppOptions options;
//         options.title = "My app";
//         return silta::ios_main(argc, argv, options, [&] { /* silta UI */ });
//     }
//
// The entry point must be called from the app's main() before any UI exists.
// It installs the UIApplication event loop, renders the frame callback on
// demand (Silta's render-on-demand applies: idle apps draw nothing), keeps the
// root window inside the safe areas and the on-screen keyboard, and only
// returns when the application terminates. The Info.plist must not declare a
// UIApplicationSceneManifest: the delegate uses the legacy window lifecycle.
#include "silta/app.hpp"

namespace silta {

// Runs the iOS application loop. `frame` is called once per rendered frame,
// like App::run's callback. Returns after UIApplicationMain returns.
int ios_main(int argc, char **argv, const AppOptions &options, const std::function<void()> &frame);

} // namespace silta
