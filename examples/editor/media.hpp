#pragma once
// Silta Cut: media access on top of FFmpeg (libavformat, libavcodec,
// libswscale, libswresample).
#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include "project.hpp"

namespace cut {

// Reads the container and returns what the editor needs to know.
std::optional<MediaInfo> probe(const std::string &path, std::string *error = nullptr);

// An RGBA8 picture with the source time it shows.
struct Picture {
  std::vector<uint8_t> rgba;
  int width = 0, height = 0;
  double time = -1;
  bool valid() const { return width > 0 && height > 0; }
};

class VideoDecoder {
public:
  VideoDecoder();
  ~VideoDecoder();
  VideoDecoder(const VideoDecoder &) = delete;
  VideoDecoder &operator=(const VideoDecoder &) = delete;

  // Output pictures are scaled to fit max_width x max_height (aspect kept).
  bool open(const std::string &path, int max_width, int max_height);
  void close();
  bool is_open() const;
  const std::string &path() const;
  // The frame visible at source time t. Seeks only when t jumps backwards or
  // far ahead; playing forward just keeps decoding.
  bool frame_at(double t, Picture &out);
  double frame_duration() const;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

// Audio as 48 kHz interleaved stereo float, from any source time.
class AudioReader {
public:
  static constexpr int rate = 48000;
  AudioReader();
  ~AudioReader();
  AudioReader(const AudioReader &) = delete;
  AudioReader &operator=(const AudioReader &) = delete;

  bool open(const std::string &path);
  bool is_open() const;
  // Writes `frames` stereo frames from source time t (silence past the end).
  void read(double t, float *out, int frames);

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

// Thumbnails and waveform, computed in the background after an import.
struct Analysis {
  static constexpr int thumb_w = 160, thumb_h = 90;
  std::vector<Picture> thumbs; // evenly spaced
  double thumb_interval = 1;   // seconds between thumbnails
  std::vector<float> peaks;    // one per 1/100 s, 0..1
  std::atomic<bool> done{false};
};
void analyze(const MediaInfo &info, Analysis &out, const std::atomic<bool> &cancel);

} // namespace cut
