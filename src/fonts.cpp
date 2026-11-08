#include "fonts.hpp"
#include <cstdlib>
#include <filesystem>
#include <system_error>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <clocale>
#endif

namespace silta::detail {
namespace {
std::filesystem::path from_utf8(const std::string &s) {
  return std::filesystem::path(std::u8string(s.begin(), s.end()));
}
bool exists(const std::string &utf8_path) {
  std::error_code ec;
  return std::filesystem::is_regular_file(from_utf8(utf8_path), ec);
}

enum class Cjk { simplified, traditional, japanese, korean };

// The user's interface language decides which CJK font draws shared Han
// characters (they differ in shape between Chinese, Japanese and Korean).
Cjk user_cjk() {
#ifdef _WIN32
  const LANGID lang = GetUserDefaultUILanguage();
  switch (PRIMARYLANGID(lang)) {
  case LANG_JAPANESE: return Cjk::japanese;
  case LANG_KOREAN: return Cjk::korean;
  case LANG_CHINESE:
    return SUBLANGID(lang) == SUBLANG_CHINESE_SIMPLIFIED || SUBLANGID(lang) == SUBLANG_CHINESE_SINGAPORE
               ? Cjk::simplified
               : Cjk::traditional;
  default: return Cjk::simplified;
  }
#else
  std::string lang;
  for (const char *var : {"LC_ALL", "LC_MESSAGES", "LANG"})
    if (const char *v = std::getenv(var); v && *v) {
      lang = v;
      break;
    }
  if (lang.rfind("ja", 0) == 0) return Cjk::japanese;
  if (lang.rfind("ko", 0) == 0) return Cjk::korean;
  if (lang.rfind("zh_TW", 0) == 0 || lang.rfind("zh_HK", 0) == 0) return Cjk::traditional;
  return Cjk::simplified;
#endif
}
} // namespace

// ============================================================ MappedFile
std::unique_ptr<MappedFile> MappedFile::open(const std::string &utf8_path) {
  std::unique_ptr<MappedFile> f(new MappedFile());
#ifdef _WIN32
  const std::wstring wide = from_utf8(utf8_path).wstring();
  HANDLE file = CreateFileW(wide.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                            FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return nullptr;
  f->file_ = file;
  LARGE_INTEGER size{};
  if (!GetFileSizeEx(file, &size) || size.QuadPart <= 0 || size.QuadPart > 0x7fffffff) return nullptr;
  HANDLE mapping = CreateFileMappingW(file, nullptr, PAGE_READONLY, 0, 0, nullptr);
  if (!mapping) return nullptr;
  f->mapping_ = mapping;
  f->data_ = static_cast<const unsigned char *>(MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0));
  if (!f->data_) return nullptr;
  f->size_ = std::size_t(size.QuadPart);
#else
  const int fd = ::open(utf8_path.c_str(), O_RDONLY | O_CLOEXEC);
  if (fd < 0) return nullptr;
  struct stat info {};
  if (::fstat(fd, &info) != 0 || info.st_size <= 0 || info.st_size > 0x7fffffff) {
    ::close(fd);
    return nullptr;
  }
  void *p = ::mmap(nullptr, std::size_t(info.st_size), PROT_READ, MAP_PRIVATE, fd, 0);
  ::close(fd);
  if (p == MAP_FAILED) return nullptr;
  f->data_ = static_cast<const unsigned char *>(p);
  f->size_ = std::size_t(info.st_size);
#endif
  return f;
}

MappedFile::~MappedFile() {
#ifdef _WIN32
  if (data_) UnmapViewOfFile(data_);
  if (mapping_) CloseHandle(mapping_);
  if (file_) CloseHandle(file_);
#else
  if (data_) ::munmap(const_cast<unsigned char *>(data_), size_);
#endif
}

// =================================================== system fallback fonts
std::vector<SystemFont> system_fallback_fonts() {
  struct Candidate {
    std::string path;
    int index;
    bool color;
  };
  std::vector<Candidate> general, cjk[4], symbols;
#ifdef _WIN32
  wchar_t windir[MAX_PATH] = L"C:\\Windows";
  GetWindowsDirectoryW(windir, MAX_PATH);
  const std::u8string u8 = (std::filesystem::path(windir) / "Fonts").u8string();
  const std::string fonts = std::string(u8.begin(), u8.end()) + "/";
  general = {{fonts + "segoeui.ttf", 0, false},  // Latin, Greek, Cyrillic, Arabic, Hebrew, ...
             {fonts + "Nirmala.ttc", 0, false},  // Indic scripts (Windows 11)
             {fonts + "Nirmala.ttf", 0, false},  // Indic scripts (Windows 10)
             {fonts + "LeelawUI.ttf", 0, false}}; // Thai, Lao, Khmer
  cjk[int(Cjk::simplified)] = {{fonts + "msyh.ttc", 0, false}, {fonts + "simsun.ttc", 0, false}};
  cjk[int(Cjk::traditional)] = {{fonts + "msjh.ttc", 0, false}, {fonts + "mingliu.ttc", 0, false}};
  cjk[int(Cjk::japanese)] = {{fonts + "YuGothM.ttc", 0, false}, {fonts + "meiryo.ttc", 0, false},
                             {fonts + "msgothic.ttc", 0, false}};
  cjk[int(Cjk::korean)] = {{fonts + "malgun.ttf", 0, false}, {fonts + "gulim.ttc", 0, false}};
  symbols = {{fonts + "seguiemj.ttf", 0, true}, {fonts + "seguisym.ttf", 0, false}};
#elif defined(__APPLE__)
  const std::string sys = "/System/Library/Fonts/";
  general = {{sys + "SFNS.ttf", 0, false},
             {sys + "Supplemental/Arial Unicode.ttf", 0, false},
             {sys + "Kohinoor.ttc", 0, false}, // Devanagari
             {sys + "Thonburi.ttc", 0, false}};
  cjk[int(Cjk::simplified)] = {{sys + "PingFang.ttc", 0, false}, {sys + "Hiragino Sans GB.ttc", 0, false}};
  cjk[int(Cjk::traditional)] = {{sys + "PingFang.ttc", 1, false}};
  cjk[int(Cjk::japanese)] = {{sys + "ヒラギノ角ゴシック W3.ttc", 0, false}, {sys + "Hiragino Sans W3.ttc", 0, false}};
  cjk[int(Cjk::korean)] = {{sys + "AppleSDGothicNeo.ttc", 0, false}};
  symbols = {{sys + "Apple Color Emoji.ttc", 0, true}, {sys + "Apple Symbols.ttf", 0, false}};
#else
  const std::string noto = "/usr/share/fonts/opentype/noto/", noto_tt = "/usr/share/fonts/truetype/noto/";
  general = {{noto_tt + "NotoSans-Regular.ttf", 0, false},
             {"/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 0, false},
             {noto_tt + "NotoSansDevanagari-Regular.ttf", 0, false},
             {noto_tt + "NotoSansThai-Regular.ttf", 0, false}};
  // Noto Sans CJK collections order their faces JP, KR, SC, TC, HK.
  for (const std::string &dir : {noto, noto_tt, std::string("/usr/share/fonts/google-noto-cjk/"),
                                  std::string("/usr/share/fonts/noto-cjk/")}) {
    cjk[int(Cjk::japanese)].push_back({dir + "NotoSansCJK-Regular.ttc", 0, false});
    cjk[int(Cjk::korean)].push_back({dir + "NotoSansCJK-Regular.ttc", 1, false});
    cjk[int(Cjk::simplified)].push_back({dir + "NotoSansCJK-Regular.ttc", 2, false});
    cjk[int(Cjk::traditional)].push_back({dir + "NotoSansCJK-Regular.ttc", 3, false});
  }
  symbols = {{noto_tt + "NotoColorEmoji.ttf", 0, true}, {noto_tt + "NotoSansSymbols2-Regular.ttf", 0, false}};
#endif
  std::vector<SystemFont> out;
  auto take = [&](const std::vector<Candidate> &list, bool first_only) {
    for (const Candidate &c : list) {
      if (!exists(c.path)) continue;
      bool duplicate = false;
      for (const SystemFont &f : out) duplicate |= f.path == c.path && f.index == c.index;
      if (!duplicate) out.push_back({c.path, c.index, c.color});
      if (first_only) return;
    }
  };
  take(general, false);
  // The user's CJK first, then one font for each of the others.
  const Cjk mine = user_cjk();
  take(cjk[int(mine)], true);
  for (int i = 0; i < 4; ++i)
    if (i != int(mine)) take(cjk[i], true);
  take(symbols, false);
  return out;
}

} // namespace silta::detail
