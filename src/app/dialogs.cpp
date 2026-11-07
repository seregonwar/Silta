// Native file dialogs.
#include "silta/app.hpp"
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

namespace silta::detail {
extern GLFWwindow *main_window;
}

#ifdef _WIN32
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shobjidl.h>

namespace silta {
namespace {
std::wstring widen(const char *s) {
  if (!s || !*s) return {};
  const int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
  std::wstring w(size_t(n > 0 ? n - 1 : 0), L'\0');
  if (n > 1) MultiByteToWideChar(CP_UTF8, 0, s, -1, w.data(), n);
  return w;
}
std::string narrow(const wchar_t *w) {
  if (!w) return {};
  const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
  std::string s(size_t(n > 0 ? n - 1 : 0), '\0');
  if (n > 1) WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
  return s;
}
HWND parent() { return detail::main_window ? glfwGetWin32Window(detail::main_window) : nullptr; }

// COM is initialized for the dialog's duration (apartment-threaded).
struct Com {
  bool owned;
  Com() : owned(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE))) {}
  ~Com() {
    if (owned) CoUninitialize();
  }
};

void apply_filters(IFileDialog *dialog, std::span<const FileFilter> filters, std::vector<std::wstring> &storage) {
  if (filters.empty()) return;
  storage.clear();
  for (const FileFilter &f : filters) storage.push_back(widen(f.name)), storage.push_back(widen(f.patterns));
  std::vector<COMDLG_FILTERSPEC> specs;
  for (size_t i = 0; i < storage.size(); i += 2) specs.push_back({storage[i].c_str(), storage[i + 1].c_str()});
  dialog->SetFileTypes(UINT(specs.size()), specs.data());
}
} // namespace

std::vector<std::string> open_file_dialog(std::span<const FileFilter> filters, bool multiple, const char *title) {
  std::vector<std::string> result;
  Com com;
  IFileOpenDialog *dialog = nullptr;
  if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&dialog)))) return result;
  std::vector<std::wstring> storage;
  apply_filters(dialog, filters, storage);
  DWORD options = 0;
  dialog->GetOptions(&options);
  dialog->SetOptions(options | FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST | (multiple ? FOS_ALLOWMULTISELECT : 0));
  if (title) dialog->SetTitle(widen(title).c_str());
  if (SUCCEEDED(dialog->Show(parent()))) {
    IShellItemArray *items = nullptr;
    if (SUCCEEDED(dialog->GetResults(&items))) {
      DWORD count = 0;
      items->GetCount(&count);
      for (DWORD i = 0; i < count; ++i) {
        IShellItem *item = nullptr;
        if (SUCCEEDED(items->GetItemAt(i, &item))) {
          PWSTR path = nullptr;
          if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
            result.push_back(narrow(path));
            CoTaskMemFree(path);
          }
          item->Release();
        }
      }
      items->Release();
    }
  }
  dialog->Release();
  return result;
}

std::optional<std::string> save_file_dialog(std::span<const FileFilter> filters, const char *default_name,
                                            const char *title) {
  std::optional<std::string> result;
  Com com;
  IFileSaveDialog *dialog = nullptr;
  if (FAILED(CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&dialog)))) return result;
  std::vector<std::wstring> storage;
  apply_filters(dialog, filters, storage);
  DWORD options = 0;
  dialog->GetOptions(&options);
  dialog->SetOptions(options | FOS_FORCEFILESYSTEM | FOS_OVERWRITEPROMPT);
  if (default_name) dialog->SetFileName(widen(default_name).c_str());
  if (title) dialog->SetTitle(widen(title).c_str());
  // Add the first pattern's extension when the user types a bare name.
  if (!filters.empty()) {
    std::wstring ext = widen(filters[0].patterns);
    if (const size_t dot = ext.find(L'.'); dot != std::wstring::npos) {
      ext = ext.substr(dot + 1);
      ext = ext.substr(0, ext.find(L';'));
      dialog->SetDefaultExtension(ext.c_str());
    }
  }
  if (SUCCEEDED(dialog->Show(parent()))) {
    IShellItem *item = nullptr;
    if (SUCCEEDED(dialog->GetResult(&item))) {
      PWSTR path = nullptr;
      if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
        result = narrow(path);
        CoTaskMemFree(path);
      }
      item->Release();
    }
  }
  dialog->Release();
  return result;
}

} // namespace silta

#else

namespace silta {
// TODO: native dialogs for macOS (NSOpenPanel) and Linux (xdg-desktop-portal).
std::vector<std::string> open_file_dialog(std::span<const FileFilter>, bool, const char *) { return {}; }
std::optional<std::string> save_file_dialog(std::span<const FileFilter>, const char *, const char *) { return {}; }
} // namespace silta

#endif
