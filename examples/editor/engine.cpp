#include "engine.hpp"
#include <chrono>
#include <deque>
#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_DECODING
#define MA_NO_ENCODING
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_NODE_GRAPH
#define MA_NO_ENGINE
// Playback is constructed on the UI thread. Keep that thread in STA so the
// Windows shell file dialogs can run there; miniaudio defaults to MTA and
// would otherwise make a later CoInitializeEx(STA) fail with CHANGED_MODE.
#ifdef _WIN32
#define MA_COINIT_VALUE 2 // COINIT_APARTMENTTHREADED
#endif
#include <miniaudio.h>

namespace cut {
Picture Renderer::video(const Project &p, double t, int width, int height) {
  Picture result;
  result.width = width; result.height = height; result.time = t;
  result.rgba.assign(size_t(width)*height*4, 0);
  for (size_t i = 3; i < result.rgba.size(); i += 4) result.rgba[i] = 255;
  for (int tr = int(p.tracks.size())-1; tr >= 0; --tr) {
    if (p.tracks[tr].kind != TrackKind::video || p.tracks[tr].hidden) continue;
    for (const Clip &c : p.clips) {
      if (c.track != tr || t < c.start || t >= c.end() || c.opacity <= 0) continue;
      const auto &m = p.media[c.media];
      if (!m.has_video) continue;
      // Include dimensions in the key, so a renderer can change output size safely.
      auto key = m.path + "|" + std::to_string(width) + "x" + std::to_string(height);
      auto &decoder = videos_[key];
      if (!decoder) { decoder = std::make_unique<VideoDecoder>(); decoder->open(m.path, width, height); }
      Picture image;
      if (!decoder->frame_at(c.in + t-c.start, image)) continue;
      // Enlarge low resolution sources with nearest neighbour; FFmpeg scales down.
      double k = std::min(double(width)/image.width, double(height)/image.height);
      int w = std::max(1, int(image.width*k)), h = std::max(1, int(image.height*k));
      int x0 = (width-w)/2, y0 = (height-h)/2;
      for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
        size_t src = (size_t(y*image.height/h)*image.width + x*image.width/w)*4;
        size_t dst = (size_t(y+y0)*width+x+x0)*4;
        float alpha = std::clamp(c.opacity, 0.f, 1.f)*image.rgba[src+3]/255.f;
        for (int ch = 0; ch < 3; ++ch)
          result.rgba[dst+ch] = uint8_t(image.rgba[src+ch]*alpha + result.rgba[dst+ch]*(1-alpha));
      }
    }
  }
  return result;
}

void Renderer::audio(const Project &p, double t, float *out, int frames) {
  std::fill(out, out+size_t(frames)*2, 0.f);
  std::vector<float> buf(size_t(frames)*2);
  for (const Clip &c : p.clips) {
    const auto &m = p.media[c.media];
    if (!m.has_audio || p.tracks[c.track].muted || c.volume <= 0) continue;
    int first = std::clamp(int(std::ceil((c.start-t)*AudioReader::rate-1e-7)), 0, frames);
    int last = std::clamp(int(std::ceil((c.end()-t)*AudioReader::rate-1e-7)), 0, frames);
    if (last <= first) continue;
    auto &reader = audios_[m.path];
    if (!reader) { reader = std::make_unique<AudioReader>(); reader->open(m.path); }
    reader->read(c.in+t+double(first)/AudioReader::rate-c.start, buf.data(), last-first);
    for (int i = 0; i < (last-first)*2; ++i) out[first*2+i] += buf[i]*c.volume;
  }
  for (int i = 0; i < frames*2; ++i) out[i] = std::clamp(out[i], -1.f, 1.f);
}

struct Playback::Impl {
  mutable std::mutex mutex;
  Project project;
  Picture image;
  uint64_t version = 0, revision = 1;
  bool play = false, stop = false, device_ok = false;
  double time = 0, produced = 0;
  std::deque<float> queue;
  ma_device device{};
  std::thread worker;

  static void callback(ma_device *dev, void *output, const void *, ma_uint32 count) {
    auto &s = *static_cast<Impl *>(dev->pUserData);
    auto *out = static_cast<float *>(output);
    std::fill(out, out+size_t(count)*2, 0.f);
    std::unique_lock lock(s.mutex, std::try_to_lock);
    if (!lock || !s.play) return;
    size_t n = std::min(size_t(count)*2, s.queue.size());
    for (size_t i = 0; i < n; ++i) { out[i] = s.queue.front(); s.queue.pop_front(); }
    s.time = std::min(s.project.duration(), s.time + double(n/2)/AudioReader::rate);
    if (s.time >= s.project.duration()-1e-7) s.play = false;
  }
  Impl() {
    auto cfg = ma_device_config_init(ma_device_type_playback);
    cfg.playback.format = ma_format_f32; cfg.playback.channels = 2;
    cfg.sampleRate = AudioReader::rate; cfg.dataCallback = callback; cfg.pUserData = this;
    cfg.periodSizeInFrames = 512;
    if (ma_device_init(nullptr, &cfg, &device) == MA_SUCCESS) {
      device_ok = ma_device_start(&device) == MA_SUCCESS;
      if (!device_ok) ma_device_uninit(&device);
    }
    worker = std::thread([this] { run(); });
  }
  ~Impl() {
    { std::lock_guard lock(mutex); stop = true; }
    worker.join();
    if (device_ok) ma_device_uninit(&device);
  }
  void run() {
    Renderer render;
    double drawn = -1;
    uint64_t drawn_revision = 0;
    auto prev = std::chrono::steady_clock::now();
    for (;;) {
      Project snapshot;
      double t, audio_t; uint64_t rev; bool fill_audio, redraw;
      {
        std::lock_guard lock(mutex);
        if (stop) break;
        auto now = std::chrono::steady_clock::now();
        if (!device_ok && play) {
          time = std::min(project.duration(), time+std::chrono::duration<double>(now-prev).count());
          if (time >= project.duration()) play = false;
        }
        prev = now;
        t = time; audio_t = produced; rev = revision;
        fill_audio = device_ok && play && queue.size() < 16384 && produced < project.duration();
        redraw = rev != drawn_revision || std::abs(t-drawn) >= 1.0/project.fps;
        if (fill_audio || redraw) snapshot = project;
      }
      if (fill_audio) {
        int n = std::min(2048, int(std::ceil((snapshot.duration()-audio_t)*AudioReader::rate)));
        std::vector<float> samples(size_t(n)*2);
        render.audio(snapshot, audio_t, samples.data(), n);
        std::lock_guard lock(mutex);
        if (rev == revision) { queue.insert(queue.end(), samples.begin(), samples.end()); produced += double(n)/AudioReader::rate; }
      }
      if (redraw) {
        Picture pic = render.video(snapshot, t, 960, 540);
        std::lock_guard lock(mutex);
        if (rev == revision) { image = std::move(pic); ++version; drawn = t; drawn_revision = rev; }
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
  }
};
Playback::Playback() : impl_(std::make_unique<Impl>()) {}
Playback::~Playback() = default;
void Playback::set_project(const Project &p) {
  std::lock_guard lock(impl_->mutex);
  impl_->project = p; impl_->time = std::min(impl_->time, p.duration());
  impl_->produced = impl_->time; impl_->queue.clear(); ++impl_->revision;
}
void Playback::seek(double t) {
  std::lock_guard lock(impl_->mutex);
  impl_->time = std::clamp(t, 0.0, impl_->project.duration());
  impl_->produced = impl_->time; impl_->queue.clear(); ++impl_->revision;
}
void Playback::toggle() {
  std::lock_guard lock(impl_->mutex);
  if (impl_->time >= impl_->project.duration()) {
    impl_->time = impl_->produced = 0; impl_->queue.clear(); ++impl_->revision;
  }
  impl_->play = !impl_->play;
}
bool Playback::playing() const { std::lock_guard lock(impl_->mutex); return impl_->play; }
double Playback::position() const { std::lock_guard lock(impl_->mutex); return impl_->time; }
bool Playback::picture(Picture &out, uint64_t &version) const {
  std::lock_guard lock(impl_->mutex);
  if (version == impl_->version) return false;
  out = impl_->image; version = impl_->version; return true;
}
bool Playback::audio_device() const { return impl_->device_ok; }
} // namespace cut
