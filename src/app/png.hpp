#pragma once
// Minimal PNG writer (RGBA8, stored deflate blocks): no dependencies, used for
// screenshots and visual regression tests (internal).
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace silta::detail {

inline bool write_png(const char *path, const uint8_t *rgba, int width, int height) {
  auto crc = [](const uint8_t *data, size_t n, uint32_t c = 0xffffffffu) {
    static uint32_t table[256];
    if (!table[1])
      for (uint32_t i = 0; i < 256; ++i) {
        uint32_t v = i;
        for (int k = 0; k < 8; ++k) v = v & 1 ? 0xedb88320u ^ (v >> 1) : v >> 1;
        table[i] = v;
      }
    for (size_t i = 0; i < n; ++i) c = table[(c ^ data[i]) & 0xff] ^ (c >> 8);
    return c;
  };
  auto be32 = [](std::vector<uint8_t> &out, uint32_t v) {
    for (int s = 24; s >= 0; s -= 8) out.push_back(uint8_t(v >> s));
  };
  auto chunk = [&](std::vector<uint8_t> &out, const char *type, const std::vector<uint8_t> &data) {
    be32(out, uint32_t(data.size()));
    std::vector<uint8_t> body(type, type + 4);
    body.insert(body.end(), data.begin(), data.end());
    out.insert(out.end(), body.begin(), body.end());
    be32(out, crc(body.data(), body.size()) ^ 0xffffffffu);
  };

  // Raw scanlines with filter byte 0.
  std::vector<uint8_t> raw;
  raw.reserve(size_t(height) * (size_t(width) * 4 + 1));
  for (int y = 0; y < height; ++y) {
    raw.push_back(0);
    raw.insert(raw.end(), rgba + size_t(y) * width * 4, rgba + size_t(y + 1) * width * 4);
  }
  // zlib stream of stored (uncompressed) blocks.
  std::vector<uint8_t> z = {0x78, 0x01};
  uint32_t a = 1, b = 0;
  for (uint8_t v : raw) a = (a + v) % 65521, b = (b + a) % 65521;
  for (size_t pos = 0; pos < raw.size() || pos == 0;) {
    const size_t n = std::min<size_t>(65535, raw.size() - pos);
    z.push_back(pos + n >= raw.size() ? 1 : 0);
    z.push_back(uint8_t(n)), z.push_back(uint8_t(n >> 8));
    z.push_back(uint8_t(~n)), z.push_back(uint8_t(~n >> 8));
    z.insert(z.end(), raw.begin() + pos, raw.begin() + pos + n);
    pos += n;
    if (raw.empty()) break;
  }
  be32(z, (b << 16) | a);

  std::vector<uint8_t> png = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
  std::vector<uint8_t> ihdr;
  be32(ihdr, uint32_t(width));
  be32(ihdr, uint32_t(height));
  ihdr.insert(ihdr.end(), {8, 6, 0, 0, 0}); // 8-bit RGBA
  chunk(png, "IHDR", ihdr);
  chunk(png, "IDAT", z);
  chunk(png, "IEND", {});

  std::FILE *f = std::fopen(path, "wb");
  if (!f) return false;
  const bool ok = std::fwrite(png.data(), 1, png.size(), f) == png.size();
  std::fclose(f);
  return ok;
}

} // namespace silta::detail
