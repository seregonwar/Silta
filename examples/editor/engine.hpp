#pragma once
#include "media.hpp"
#include <map>
#include <mutex>
#include <thread>

namespace cut {
// Each renderer belongs to exactly one worker. Export and preview never share decoders.
class Renderer {
public:
  Picture video(const Project &, double t, int width, int height);
  void audio(const Project &, double t, float *out, int frames);
private:
  std::map<std::string, std::unique_ptr<VideoDecoder>> videos_;
  std::map<std::string, std::unique_ptr<AudioReader>> audios_;
};

class Playback {
public:
  Playback();
  ~Playback();
  void set_project(const Project &);
  void seek(double);
  void toggle();
  bool playing() const;
  double position() const;
  bool picture(Picture &, uint64_t &version) const;
  bool audio_device() const;
private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

class Export {
public:
  ~Export();
  void start(Project, std::string path);
  void cancel() { cancel_ = true; }
  bool running() const { return running_; }
  float progress() const { return progress_; }
  bool finished() const { return finished_; }
  std::string error() const; // read after finished()
private:
  std::thread worker_;
  std::atomic<bool> cancel_{false}, running_{false}, finished_{false};
  std::atomic<float> progress_{0};
  std::string error_;
};
} // namespace cut
