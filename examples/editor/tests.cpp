#include "media.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
void require(bool ok, const char *msg) { if (!ok) throw std::runtime_error(msg); }
int main(int argc, char **argv) {
  try {
    cut::Project p;
    cut::History h;
    p.media.push_back({"test", "test", 4, true, true});
    h.record(p);
    int id = cut::add_clip(p, 0);
    require(p.duration() == 4, "add duration");
    h.record(p);
    require(cut::split(p, 1, id) == 1 && p.clips.size() == 2, "split");
    require(p.clips[1].in == 1 && p.clips[1].start == 1, "split source alignment");
    require(h.undo(p) && p.clips.size() == 1, "undo");
    require(h.redo(p) && p.clips.size() == 2, "redo");
    cut::ripple_delete(p, id);
    require(p.clips.size() == 1 && p.clips[0].start == 0 && p.duration() == 3, "ripple");
    require(!cut::probe("__missing__.mp4"), "missing input");
    for (int a = 1; a < argc; ++a) {
      auto info = cut::probe(argv[a]);
      require(info.has_value() && info->duration > 0, "probe");
      if (info->has_video) {
        cut::VideoDecoder d;
        require(d.open(argv[a], 320, 180), "video open");
        for (double t : {0.0, 0.02, 1.01, 0.3, 2.0, 0.0}) {
          if (t >= info->duration) continue;
          cut::Picture pic;
          require(d.frame_at(t, pic) && pic.valid(), "decode");
          require(pic.time <= t + 1e-5 && t - pic.time < d.frame_duration() + 1e-4, "exact visible frame");
        }
      }
      if (info->has_audio) {
        cut::AudioReader r;
        require(r.open(argv[a]), "audio open");
        std::vector<float> buf(9600);
        r.read(0.2, buf.data(), 4800);
        double energy = 0;
        for (float v : buf) { require(std::isfinite(v), "finite audio"); energy += v*v; }
        require(energy > 0.001, "decoded audio energy");
        cut::AudioReader continuous;
        require(continuous.open(argv[a]), "continuous audio open");
        int reference_frames = int(std::min(2.0, info->duration)*cut::AudioReader::rate);
        std::vector<float> reference(size_t(reference_frames)*2);
        continuous.read(0, reference.data(), reference_frames);
        for (double t : {0.2, 1.0, 0.3}) {
          r.read(t, buf.data(), 4800);
          size_t offset = size_t(std::llround(t*cut::AudioReader::rate))*2;
          if (offset+buf.size()>reference.size()) continue;
          double difference=0, reference_energy=0;
          for (size_t i=0;i<buf.size();++i) {
            double v=reference[offset+i]; reference_energy+=v*v;
            double delta=buf[i]-v; difference+=delta*delta;
          }
          if (difference/std::max(0.001,reference_energy)>=0.01)
            std::cerr<<info->name<<" seek="<<t<<" error="<<difference/std::max(0.001,reference_energy)<<'\n';
          require(difference/std::max(0.001,reference_energy)<0.01,"audio seek sample alignment");
        }
        r.read(info->duration + 1, buf.data(), 4800);
        for (float v : buf) require(v == 0, "silence past end");
      }
      cut::Analysis analysis;
      std::atomic<bool> cancel{false};
      cut::analyze(*info, analysis, cancel);
      require(analysis.done && (!info->has_video || !analysis.thumbs.empty()) &&
              (!info->has_audio || !analysis.peaks.empty()), "analysis");
      std::cout << info->name << " " << info->duration << "s OK\n";
    }
    std::cout << "All media/model tests passed\n";
    return 0;
  } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
