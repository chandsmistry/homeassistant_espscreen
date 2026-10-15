#pragma once
// Strict baseline JPEG envelope checks before the P4 hardware decoder sees untrusted bytes.
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
namespace still_jpeg {
constexpr size_t MAX_JPEG_BYTES = 1024 * 1024;
constexpr unsigned MAX_SIDE = 4096;
constexpr size_t MAX_PIXELS = 2048 * 2048;
struct Info { unsigned width = 0, height = 0, encoded_width = 0, encoded_height = 0; bool ok = false; };
inline Info inspect(const uint8_t *p, size_t n) {
  Info out;
  if (!p || n < 4 || n > MAX_JPEG_BYTES || p[0] != 255 || p[1] != 216) return out;
  size_t at = 2;
  bool frame = false, scan = false, crop = false;
  while (at < n) {
    if (p[at++] != 255) return {};
    while (at < n && p[at] == 255) ++at;
    if (at >= n) return {};
    const unsigned marker = p[at++];
    if (marker == 217) {
      if (!frame || !scan || at != n) return {};
      if (!crop) { out.width = out.encoded_width; out.height = out.encoded_height; }
      if (!out.width || !out.height || out.width > out.encoded_width || out.height > out.encoded_height ||
          (crop && (((out.width + 7) & ~7u) != out.encoded_width ||
                    ((out.height + 7) & ~7u) != out.encoded_height))) return {};
      out.ok = true;
      return out;
    }
    if (marker == 0 || marker == 216 || (marker >= 208 && marker <= 215)) return {};
    if (at + 2 > n) return {};
    const size_t len = (size_t(p[at]) << 8) | p[at + 1];
    if (len < 2 || len > n - at) return {};
    // Self-contained logical dimensions for an image padded to the decoder's 8-pixel size constraint.
    if (marker == 239 && len >= 10 && std::memcmp(p + at + 2, "MNETCROP", 8) == 0) {
      if (crop || scan || len != 14) return {};
      out.width = (unsigned(p[at + 10]) << 8) | p[at + 11];
      out.height = (unsigned(p[at + 12]) << 8) | p[at + 13];
      crop = true;
    }
    // Adobe RGB-coded JPEGs are not YCbCr, even with a baseline RGB-looking Pillow mode.
    if (marker == 238 && len >= 14 && std::memcmp(p + at + 2, "Adobe", 5) == 0 && p[at + 13] != 1) return {};
    if (marker >= 192 && marker <= 207 && marker != 196 && marker != 200 && marker != 204) {
      if (marker != 192 || frame || len != 17 || p[at + 2] != 8 || p[at + 7] != 3) return {};
      if (p[at + 8] != 1 || p[at + 11] != 2 || p[at + 14] != 3 ||
          (p[at + 9] != 0x11 && p[at + 9] != 0x21 && p[at + 9] != 0x22) ||
          p[at + 12] != 0x11 || p[at + 15] != 0x11) return {};
      out.encoded_height = (unsigned(p[at + 3]) << 8) | p[at + 4];
      out.encoded_width = (unsigned(p[at + 5]) << 8) | p[at + 6];
      const size_t pixels = size_t(out.encoded_width) * out.encoded_height;
      if (!out.encoded_width || !out.encoded_height || out.encoded_width > MAX_SIDE ||
          out.encoded_height > MAX_SIDE || pixels > MAX_PIXELS || pixels % 8) return {};
      frame = true;
    }
    if (marker == 218) {
      if (!frame || scan || len != 12 || p[at + 2] != 3 || p[at + 3] != 1 ||
          p[at + 5] != 2 || p[at + 7] != 3 || p[at + len - 3] != 0 ||
          p[at + len - 2] != 63 || p[at + len - 1] != 0) return {};
      for (size_t offset : {size_t(4), size_t(6), size_t(8)})
        if ((p[at + offset] >> 4) > 3 || (p[at + offset] & 15) > 3) return {};
      scan = true;
      at += len;
      while (at < n) {
        if (p[at++] != 255) continue;
        const size_t start = at - 1;
        while (at < n && p[at] == 255) ++at;
        if (at >= n) return {};
        if (p[at] == 0 || (p[at] >= 208 && p[at] <= 215)) { ++at; continue; }
        at = start;
        break;
      }
      continue;
    }
    at += len;
  }
  return {};
}
// A hardware output can have MCU padding on both axes. Never copy padding into LVGL's packed rows.
inline bool copy_rows(uint16_t *dst, size_t count, const uint16_t *src, size_t available,
                      unsigned w, unsigned h, unsigned stride) {
  if (!dst || !src || !w || !h || stride < w || size_t(w) * h > count ||
      size_t(stride) * h > available) return false;
  for (unsigned y = 0; y < h; ++y)
    for (unsigned x = 0; x < w; ++x) dst[size_t(y) * w + x] = src[size_t(y) * stride + x];
  return true;
}
}  // namespace still_jpeg
