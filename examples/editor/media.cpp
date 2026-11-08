#include "media.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

namespace cut {
namespace {
std::string av_error(int err) {
  char buf[AV_ERROR_MAX_STRING_SIZE] = {};
  av_strerror(err, buf, sizeof buf);
  return buf;
}

struct Quiet {
  Quiet() { av_log_set_level(AV_LOG_ERROR); }
} quiet;

double seconds(int64_t ts, AVRational tb) { return ts == AV_NOPTS_VALUE ? 0.0 : double(ts) * av_q2d(tb); }

// The pieces shared by the video and audio readers: a demuxer and the
// decoder of one stream.
struct Stream {
  AVFormatContext *fmt = nullptr;
  AVCodecContext *dec = nullptr;
  AVStream *st = nullptr;
  int index = -1;
  AVPacket *pkt = av_packet_alloc();
  AVFrame *frame = av_frame_alloc();
  double start = 0; // stream start time, seconds
  bool eof = false, flushed = false;

  ~Stream() { close(); av_packet_free(&pkt); av_frame_free(&frame); }

  bool open(const std::string &path, AVMediaType type) {
    close();
    if (avformat_open_input(&fmt, path.c_str(), nullptr, nullptr) < 0) return false;
    if (avformat_find_stream_info(fmt, nullptr) < 0) return close(), false;
    const AVCodec *codec = nullptr;
    index = av_find_best_stream(fmt, type, -1, -1, &codec, 0);
    if (index < 0 || !codec) return close(), false;
    st = fmt->streams[index];
    dec = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(dec, st->codecpar);
    dec->thread_count = 0; // automatic
    dec->pkt_timebase = st->time_base;
    if (avcodec_open2(dec, codec, nullptr) < 0) return close(), false;
    start = st->start_time == AV_NOPTS_VALUE ? 0.0 : seconds(st->start_time, st->time_base);
    eof = flushed = false;
    return true;
  }
  void close() {
    avcodec_free_context(&dec);
    if (fmt) avformat_close_input(&fmt);
    st = nullptr;
    index = -1;
  }
  void seek(double t) {
    const int64_t ts = int64_t((t + start) / av_q2d(st->time_base));
    av_seek_frame(fmt, index, ts, AVSEEK_FLAG_BACKWARD);
    avcodec_flush_buffers(dec);
    eof = flushed = false;
  }
  // Next decoded frame into `frame`; false at the end of the stream.
  bool next() {
    for (;;) {
      const int r = avcodec_receive_frame(dec, frame);
      if (r == 0) return true;
      if (r == AVERROR_EOF) return false;
      if (r != AVERROR(EAGAIN)) return false;
      if (eof) {
        if (flushed) return false;
        avcodec_send_packet(dec, nullptr);
        flushed = true;
        continue;
      }
      const int rr = av_read_frame(fmt, pkt);
      if (rr < 0) {
        eof = true;
        continue;
      }
      if (pkt->stream_index == index) avcodec_send_packet(dec, pkt);
      av_packet_unref(pkt);
    }
  }
  double frame_time() const {
    const int64_t ts = frame->best_effort_timestamp != AV_NOPTS_VALUE ? frame->best_effort_timestamp : frame->pts;
    return seconds(ts, st->time_base) - start;
  }
};
} // namespace

// ================================================================ probe
std::optional<MediaInfo> probe(const std::string &path, std::string *error) {
  AVFormatContext *fmt = nullptr;
  int r = avformat_open_input(&fmt, path.c_str(), nullptr, nullptr);
  if (r < 0) {
    if (error) *error = av_error(r);
    return std::nullopt;
  }
  if ((r = avformat_find_stream_info(fmt, nullptr)) < 0) {
    if (error) *error = av_error(r);
    avformat_close_input(&fmt);
    return std::nullopt;
  }
  MediaInfo info;
  info.path = path;
  const std::u8string name = std::filesystem::path(std::u8string(path.begin(), path.end())).filename().u8string();
  info.name.assign(name.begin(), name.end());
  if (fmt->duration != AV_NOPTS_VALUE) info.duration = double(fmt->duration) / AV_TIME_BASE;
  const int v = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
  const int a = av_find_best_stream(fmt, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
  if (v >= 0 && !(fmt->streams[v]->disposition & AV_DISPOSITION_ATTACHED_PIC)) {
    const AVStream *st = fmt->streams[v];
    info.has_video = true;
    info.width = st->codecpar->width;
    info.height = st->codecpar->height;
    const AVRational fr = st->avg_frame_rate.num ? st->avg_frame_rate : st->r_frame_rate;
    info.fps = fr.num && fr.den ? av_q2d(fr) : 30.0;
    if (info.duration <= 0 && st->duration != AV_NOPTS_VALUE) info.duration = seconds(st->duration, st->time_base);
  }
  if (a >= 0) {
    const AVStream *st = fmt->streams[a];
    info.has_audio = true;
    info.sample_rate = st->codecpar->sample_rate;
    info.channels = st->codecpar->ch_layout.nb_channels;
    if (info.duration <= 0 && st->duration != AV_NOPTS_VALUE) info.duration = seconds(st->duration, st->time_base);
  }
  avformat_close_input(&fmt);
  if (!info.has_video && !info.has_audio) {
    if (error) *error = "no audio or video stream";
    return std::nullopt;
  }
  if (info.duration <= 0) info.duration = info.has_video ? 5.0 : 0.0; // still images
  return info;
}

// ========================================================= VideoDecoder
struct VideoDecoder::Impl {
  Stream s;
  std::string path;
  SwsContext *sws = nullptr;
  int out_w = 0, out_h = 0;
  double fd = 1.0 / 30; // frame duration
  bool have = false;    // `frame` holds a decoded picture
  double have_time = -1;
  Picture last;

  ~Impl() { sws_freeContext(sws); }

  void convert(Picture &out) {
    AVFrame *f = s.frame;
    sws = sws_getCachedContext(sws, f->width, f->height, AVPixelFormat(f->format), out_w, out_h, AV_PIX_FMT_RGBA,
                               SWS_BILINEAR, nullptr, nullptr, nullptr);
    out.width = out_w;
    out.height = out_h;
    out.rgba.resize(size_t(out_w) * size_t(out_h) * 4);
    uint8_t *dst[4] = {out.rgba.data(), nullptr, nullptr, nullptr};
    const int stride[4] = {out_w * 4, 0, 0, 0};
    sws_scale(sws, f->data, f->linesize, 0, f->height, dst, stride);
    out.time = have_time;
  }
};

VideoDecoder::VideoDecoder() : impl_(std::make_unique<Impl>()) {}
VideoDecoder::~VideoDecoder() = default;

bool VideoDecoder::open(const std::string &path, int max_w, int max_h) {
  Impl &d = *impl_;
  d.have = false;
  d.last = {};
  d.path = path;
  if (!d.s.open(path, AVMEDIA_TYPE_VIDEO)) return false;
  const int w = d.s.dec->width, h = d.s.dec->height;
  const double k = std::min({1.0, double(max_w) / w, double(max_h) / h});
  d.out_w = std::max(2, int(w * k) & ~1);
  d.out_h = std::max(2, int(h * k) & ~1);
  const AVRational fr = d.s.st->avg_frame_rate.num ? d.s.st->avg_frame_rate : d.s.st->r_frame_rate;
  d.fd = fr.num && fr.den ? 1.0 / av_q2d(fr) : 1.0 / 30;
  return true;
}

void VideoDecoder::close() { impl_->s.close(), impl_->have = false, impl_->path.clear(); }
bool VideoDecoder::is_open() const { return impl_->s.dec != nullptr; }
const std::string &VideoDecoder::path() const { return impl_->path; }
double VideoDecoder::frame_duration() const { return impl_->fd; }

bool VideoDecoder::frame_at(double t, Picture &out) {
  Impl &d = *impl_;
  if (!is_open()) return false;
  t = std::max(0.0, t);
  // Keep one decoded frame ahead: the visible frame starts at/before t.
  if (!d.have || t < d.last.time - 1e-6 || t > d.have_time + 2.0) {
    d.s.seek(t);
    d.have = false;
    d.last = {};
  }
  for (;;) {
    if (!d.have) {
      if (!d.s.next()) break;
      d.have_time = d.s.frame_time();
      d.have = true;
    }
    if (d.have_time > t + 1e-6 && d.last.valid()) break;
    d.convert(d.last);
    d.have = false;
  }
  if (!d.last.valid()) return false;
  out = d.last;
  return true;
}
// ========================================================== AudioReader
struct AudioReader::Impl {
  Stream s;
  SwrContext *swr = nullptr;
  std::vector<float> buf; // interleaved stereo
  double buf_time = -1;   // time of buf[0]
  double expect = -1;     // where the next read should start
  double output_end = 0;
  bool drained = false;

  ~Impl() { swr_free(&swr); }

  bool make_swr() {
    swr_free(&swr);
    AVChannelLayout out = AV_CHANNEL_LAYOUT_STEREO;
    return swr_alloc_set_opts2(&swr, &out, AV_SAMPLE_FMT_FLT, rate, &s.dec->ch_layout, s.dec->sample_fmt,
                               s.dec->sample_rate, 0, nullptr) >= 0 &&
           swr_init(swr) >= 0;
  }
  // Decodes one more frame into buf. False at the end.
  bool decode_more() {
    if (!s.next()) {
      if (drained) return false;
      drained = true;
      const int capacity = swr_get_out_samples(swr, 0);
      const size_t at = buf.size();
      if (buf.empty()) buf_time = output_end;
      buf.resize(at + size_t(std::max(0, capacity))*2);
      uint8_t *dst[] = {reinterpret_cast<uint8_t *>(buf.data()+at)};
      const int n = swr_convert(swr, dst, capacity, nullptr, 0);
      buf.resize(at + size_t(std::max(0, n))*2);
      output_end = buf_time + double(buf.size()/2)/rate;
      return n > 0;
    }
    AVFrame *f = s.frame;
    const double t = s.frame_time() - double(swr_get_delay(swr, s.dec->sample_rate))/s.dec->sample_rate;
    if (buf.empty()) buf_time = t;
    const int max_out = swr_get_out_samples(swr, f->nb_samples);
    const size_t at = buf.size();
    buf.resize(at + size_t(std::max(0, max_out)) * 2);
    uint8_t *dst[1] = {reinterpret_cast<uint8_t *>(buf.data() + at)};
    const int n = swr_convert(swr, dst, max_out, const_cast<const uint8_t **>(f->extended_data), f->nb_samples);
    buf.resize(at + size_t(std::max(0, n)) * 2);
    output_end = buf_time + double(buf.size()/2)/rate;
    return true;
  }
};

AudioReader::AudioReader() : impl_(std::make_unique<Impl>()) {}
AudioReader::~AudioReader() = default;

bool AudioReader::open(const std::string &path) {
  Impl &d = *impl_;
  d.buf.clear();
  d.expect = -1;
  d.drained = false;
  return d.s.open(path, AVMEDIA_TYPE_AUDIO) && d.make_swr();
}
bool AudioReader::is_open() const { return impl_->s.dec != nullptr; }

void AudioReader::read(double t, float *out, int frames) {
  Impl &d = *impl_;
  std::fill(out, out + size_t(frames) * 2, 0.f);
  if (!is_open() || t < -1e-9) return;
  // Not continuing from the last read: seek and start over.
  if (std::abs(t - d.expect) > 0.5 / rate) {
    // AAC overlap and MP3 bit reservoirs need packets before the requested
    // sample. Decode preroll, then discard it by timestamp below.
    d.s.seek(std::max(0.0, t - 0.5));
    d.make_swr();
    d.buf.clear();
    d.buf_time = -1;
    d.drained = false;
    d.output_end = 0;
  }
  d.expect = t + double(frames) / rate;
  // Make sure the buffer covers [t, t + frames).
  auto buffered_end = [&] { return d.buf_time + double(d.buf.size() / 2) / rate; };
  while (d.buf.empty() || buffered_end() < t + double(frames) / rate)
    if (!d.decode_more()) break;
  if (d.buf.empty()) return;
  // Drop what lies before t, then copy.
  const int64_t skip = std::max<int64_t>(0, int64_t(std::llround((t - d.buf_time) * rate)));
  const int64_t avail = int64_t(d.buf.size() / 2);
  if (skip >= avail) {
    d.buf.clear();
    return;
  }
  const int64_t lead = std::max<int64_t>(0, int64_t(std::llround((d.buf_time - t) * rate))); // gap before data
  const int64_t n = std::min<int64_t>(frames - std::min<int64_t>(lead, frames), avail - skip);
  if (n > 0) std::memcpy(out + lead * 2, d.buf.data() + skip * 2, size_t(n) * 2 * sizeof(float));
  const int64_t consumed = skip + std::max<int64_t>(0, n);
  d.buf.erase(d.buf.begin(), d.buf.begin() + consumed * 2);
  d.buf_time += double(consumed) / rate;
}

// ============================================================= analyze
void analyze(const MediaInfo &info, Analysis &out, const std::atomic<bool> &cancel) {
  if (info.has_video) {
    VideoDecoder dec;
    if (dec.open(info.path, Analysis::thumb_w, Analysis::thumb_h)) {
      const int count = std::clamp(int(std::ceil(info.duration)), 1, 60);
      out.thumb_interval = info.duration / count;
      for (int i = 0; i < count && !cancel; ++i) {
        Picture p;
        if (dec.frame_at((i + 0.5) * out.thumb_interval, p)) out.thumbs.push_back(std::move(p));
      }
    }
  }
  if (info.has_audio) {
    AudioReader reader;
    if (reader.open(info.path)) {
      const int block = AudioReader::rate / 100;
      std::vector<float> samples(size_t(block) * 2);
      const int blocks = int(info.duration * 100);
      out.peaks.reserve(size_t(blocks));
      for (int i = 0; i < blocks && !cancel; ++i) {
        reader.read(i / 100.0, samples.data(), block);
        float peak = 0;
        for (float v : samples) peak = std::max(peak, std::abs(v));
        out.peaks.push_back(std::min(1.f, peak));
      }
    }
  }
  out.done = true;
}

} // namespace cut

