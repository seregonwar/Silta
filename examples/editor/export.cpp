#include "engine.hpp"
#include <filesystem>
#include <stdexcept>
#include <cstring>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}

namespace cut {
namespace {
void check(int code, const char *operation) {
  if (code >= 0) return;
  char text[AV_ERROR_MAX_STRING_SIZE]{};
  av_strerror(code, text, sizeof text);
  throw std::runtime_error(std::string(operation)+": "+text);
}
std::filesystem::path path_of(const std::string &p) { return std::filesystem::path(std::u8string(p.begin(), p.end())); }
struct Encoder {
  AVCodecContext *ctx = nullptr;
  AVStream *stream = nullptr;
  AVFrame *frame = av_frame_alloc();
  ~Encoder() { av_frame_free(&frame); avcodec_free_context(&ctx); }
  void send(AVFormatContext *mux, AVFrame *f) {
    check(avcodec_send_frame(ctx, f), "Encode frame");
    AVPacket *pkt = av_packet_alloc();
    for (;;) {
      int r = avcodec_receive_packet(ctx, pkt);
      if (r == AVERROR(EAGAIN) || r == AVERROR_EOF) break;
      if (r < 0) { av_packet_free(&pkt); check(r, "Read encoded packet"); }
      av_packet_rescale_ts(pkt, ctx->time_base, stream->time_base);
      pkt->stream_index = stream->index;
      r = av_interleaved_write_frame(mux, pkt);
      if (r < 0) { av_packet_free(&pkt); check(r, "Write MP4 packet"); }
    }
    av_packet_free(&pkt);
  }
  void open(AVFormatContext *mux, const char *name, const Project &p) {
    const AVCodec *codec = avcodec_find_encoder_by_name(name);
    if (!codec) throw std::runtime_error(std::string("Encoder unavailable: ")+name);
    ctx = avcodec_alloc_context3(codec);
    bool video = codec->type == AVMEDIA_TYPE_VIDEO;
    if (video) {
      ctx->width = p.width; ctx->height = p.height; ctx->pix_fmt = AV_PIX_FMT_YUV420P;
      ctx->framerate = av_d2q(p.fps, 100000); ctx->time_base = av_inv_q(ctx->framerate);
      ctx->bit_rate = 8000000; ctx->gop_size = int(p.fps*2); ctx->max_b_frames = 0;
    } else {
      ctx->sample_rate = AudioReader::rate; ctx->sample_fmt = AV_SAMPLE_FMT_FLTP;
      ctx->ch_layout = AV_CHANNEL_LAYOUT_STEREO; ctx->time_base = {1, AudioReader::rate}; ctx->bit_rate = 192000;
    }
    if (mux->oformat->flags & AVFMT_GLOBALHEADER) ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    check(avcodec_open2(ctx, codec, nullptr), "Open encoder");
    stream = avformat_new_stream(mux, nullptr);
    if (!stream) throw std::bad_alloc();
    stream->time_base = ctx->time_base;
    check(avcodec_parameters_from_context(stream->codecpar, ctx), "Set stream format");
    frame->format = video ? int(ctx->pix_fmt) : int(ctx->sample_fmt);
    if (video) { frame->width = ctx->width; frame->height = ctx->height; }
    else { frame->sample_rate = ctx->sample_rate; av_channel_layout_copy(&frame->ch_layout, &ctx->ch_layout); frame->nb_samples = ctx->frame_size; }
    check(av_frame_get_buffer(frame, 32), "Allocate encoder frame");
  }
};
struct Mux {
  AVFormatContext *fmt = nullptr;
  SwsContext *sws = nullptr;
  ~Mux() { sws_freeContext(sws); if (fmt) { if (fmt->pb) avio_closep(&fmt->pb); avformat_free_context(fmt); } }
};
}
Export::~Export() { cancel(); if (worker_.joinable()) worker_.join(); }
std::string Export::error() const { return finished_ ? error_ : std::string{}; }
void Export::start(Project p, std::string path) {
  if (running_) return;
  if (worker_.joinable()) worker_.join();
  cancel_ = false; progress_ = 0; finished_ = false; running_ = true; error_.clear();
  worker_ = std::thread([this, p = std::move(p), path = std::move(path)] {
    std::string partial = path + ".partial.mp4";
    bool own_partial = false;
    try {
      if (p.duration() <= 0) throw std::runtime_error("The timeline is empty");
      if (p.width <= 0 || p.height <= 0 || p.width%2 || p.height%2 || p.fps <= 0)
        throw std::runtime_error("Invalid output dimensions or frame rate");
      if (std::filesystem::exists(path_of(path)) || std::filesystem::exists(path_of(partial)))
        throw std::runtime_error("Choose a new filename: output already exists");
      for (auto &m : p.media) if (!probe(m.path)) throw std::runtime_error("Cannot read source: "+m.name);
      {
        Mux m;
        check(avformat_alloc_output_context2(&m.fmt, nullptr, "mp4", partial.c_str()), "Create MP4");
        Encoder v, a;
        v.open(m.fmt, "libopenh264", p); a.open(m.fmt, "aac", p);
        own_partial = true;
        check(avio_open(&m.fmt->pb, partial.c_str(), AVIO_FLAG_WRITE), "Open destination");
        AVDictionary *options = nullptr;
        av_dict_set(&options, "movflags", "+faststart", 0);
        int header = avformat_write_header(m.fmt, &options);
        av_dict_free(&options); check(header, "Write MP4 header");
        m.sws = sws_getContext(p.width, p.height, AV_PIX_FMT_RGBA, p.width, p.height, AV_PIX_FMT_YUV420P,
                               SWS_BILINEAR, nullptr, nullptr, nullptr);
        if (!m.sws) throw std::runtime_error("Cannot convert video");
        Renderer render;
        int64_t vi = 0, ai = 0, video_frames = int64_t(std::ceil(p.duration()*p.fps));
        int64_t audio_frames = int64_t(std::ceil(p.duration()*AudioReader::rate));
        std::vector<float> samples(size_t(a.ctx->frame_size)*2);
        while (vi < video_frames || ai < audio_frames) {
          if (cancel_) throw std::runtime_error("Export cancelled");
          if (vi < video_frames && (ai >= audio_frames || double(vi)/p.fps <= double(ai)/AudioReader::rate)) {
            Picture pic = render.video(p, double(vi)/p.fps, p.width, p.height);
            check(av_frame_make_writable(v.frame), "Prepare video frame");
            const uint8_t *data[4] = {pic.rgba.data()}; const int stride[4] = {p.width*4};
            sws_scale(m.sws, data, stride, 0, p.height, v.frame->data, v.frame->linesize);
            v.frame->pts = vi++; v.send(m.fmt, v.frame);
            progress_ = float(double(vi)/video_frames);
          } else {
            int n = int(std::min<int64_t>(a.ctx->frame_size, audio_frames-ai));
            render.audio(p, double(ai)/AudioReader::rate, samples.data(), n);
            check(av_frame_make_writable(a.frame), "Prepare audio frame");
            a.frame->nb_samples = n; a.frame->pts = ai;
            for (int i = 0; i < n; ++i) for (int ch = 0; ch < 2; ++ch)
              reinterpret_cast<float *>(a.frame->data[ch])[i] = samples[size_t(i)*2+ch];
            a.send(m.fmt, a.frame); ai += n;
          }
        }
        v.send(m.fmt, nullptr); a.send(m.fmt, nullptr);
        check(av_write_trailer(m.fmt), "Finalize MP4");
        check(avio_closep(&m.fmt->pb), "Close MP4");
      }
      if (cancel_) throw std::runtime_error("Export cancelled");
      std::filesystem::rename(path_of(partial), path_of(path));
      progress_ = 1;
    } catch (const std::exception &e) {
      error_ = e.what();
      if (own_partial) { std::error_code ec; std::filesystem::remove(path_of(partial), ec); }
    }
    running_ = false; finished_ = true;
  });
}
} // namespace cut
