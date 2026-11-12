#pragma once
// Toasts in the style of Sonner (shadcn's toast):
//
//   silta::toast("Export finished", {.description = "clip.mp4, 12 MB", .kind = ToastKind::success});
//   ...
//   silta::Toaster(); // once per frame, after the rest of the UI

namespace silta {

enum class ToastKind { normal, success, info, warning, error };

struct ToastOptions {
  const char *description = nullptr;
  ToastKind kind = ToastKind::normal;
  float duration = 4.f; // seconds
};

void toast(const char *title, const ToastOptions &options = {});

// Draws the toasts stacked in the bottom-right corner (bottom-left in RTL).
void Toaster();

} // namespace silta
