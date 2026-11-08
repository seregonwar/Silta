#pragma once
// Silta Cut: the project model. Plain data plus the edit operations the UI
// performs; every edit goes through History so it can be undone.
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace cut {

struct MediaInfo {
  std::string path, name;
  double duration = 0; // seconds
  bool has_video = false, has_audio = false;
  int width = 0, height = 0;
  double fps = 0;
  int sample_rate = 0, channels = 0;
};

enum class TrackKind { video, audio };

struct Track {
  TrackKind kind = TrackKind::video;
  std::string name;
  bool muted = false, hidden = false, locked = false;
};

struct Clip {
  int id = 0;
  int media = 0;       // index into Project::media
  int track = 0;       // index into Project::tracks
  double start = 0;    // timeline seconds
  double in = 0;       // source seconds
  double out = 0;      // source seconds (exclusive)
  float volume = 1.f;  // 0..2
  float opacity = 1.f; // 0..1 (video)
  double length() const { return out - in; }
  double end() const { return start + length(); }
};

struct Project {
  std::string name = "Untitled project";
  std::vector<MediaInfo> media;
  std::vector<Track> tracks{{TrackKind::video, "Video 2"}, {TrackKind::video, "Video 1"}, {TrackKind::audio, "Audio 1"},
                            {TrackKind::audio, "Audio 2"}};
  std::vector<Clip> clips;
  int width = 1920, height = 1080;
  double fps = 30;
  int next_clip_id = 1;

  double duration() const {
    double d = 0;
    for (const Clip &c : clips) d = std::max(d, c.end());
    return d;
  }
  Clip *find(int id) {
    for (Clip &c : clips)
      if (c.id == id) return &c;
    return nullptr;
  }
  const Clip *find(int id) const { return const_cast<Project *>(this)->find(id); }
  // Snap a time to the frame grid.
  double frame(double t) const { return std::round(t * fps) / fps; }

  // The visible video clip on top at time t (tracks are listed top to bottom).
  const Clip *video_at(double t) const {
    for (size_t tr = 0; tr < tracks.size(); ++tr) {
      if (tracks[tr].kind != TrackKind::video || tracks[tr].hidden) continue;
      for (const Clip &c : clips)
        if (c.track == int(tr) && t >= c.start && t < c.end() && media[size_t(c.media)].has_video) return &c;
    }
    return nullptr;
  }
};

// Undo/redo by snapshots of the editable state (media list included, so an
// undone import also disappears from the bin).
class History {
public:
  void record(const Project &p) {
    undo_.push_back(p);
    if (undo_.size() > 200) undo_.erase(undo_.begin());
    redo_.clear();
  }
  bool can_undo() const { return !undo_.empty(); }
  bool can_redo() const { return !redo_.empty(); }
  bool undo(Project &p) {
    if (undo_.empty()) return false;
    redo_.push_back(p);
    p = undo_.back();
    undo_.pop_back();
    return true;
  }
  bool redo(Project &p) {
    if (redo_.empty()) return false;
    undo_.push_back(p);
    p = redo_.back();
    redo_.pop_back();
    return true;
  }

private:
  std::vector<Project> undo_, redo_;
};

// ---- Edit operations (callers record History before calling).

// The first track of `kind`, preferring the lowest video track (closest to the
// audio) for new video.
inline int default_track(const Project &p, TrackKind kind) {
  int found = -1;
  for (size_t i = 0; i < p.tracks.size(); ++i)
    if (p.tracks[i].kind == kind && !p.tracks[i].locked) {
      found = int(i);
      if (kind == TrackKind::audio) break;
    }
  return found;
}

inline double track_end(const Project &p, int track) {
  double end = 0;
  for (const Clip &c : p.clips)
    if (c.track == track) end = std::max(end, c.end());
  return end;
}

// True when [start, end) on `track` is free (ignoring clip `except`).
inline bool range_free(const Project &p, int track, double start, double end, int except = 0) {
  for (const Clip &c : p.clips)
    if (c.track == track && c.id != except && start < c.end() - 1e-6 && end > c.start + 1e-6) return false;
  return true;
}

// Adds media at `start` on `track` (or at the end of its default track).
inline int add_clip(Project &p, int media, int track = -1, double start = -1) {
  if (media < 0 || media >= int(p.media.size())) return 0;
  const MediaInfo &m = p.media[size_t(media)];
  if (track < 0) track = default_track(p, m.has_video ? TrackKind::video : TrackKind::audio);
  if (track < 0 || track >= int(p.tracks.size()) || p.tracks[track].locked || m.duration <= 0) return 0;
  if (p.tracks[track].kind == TrackKind::video ? !m.has_video : !m.has_audio) return 0;
  Clip c;
  c.id = p.next_clip_id++;
  c.media = media;
  c.track = track;
  c.in = 0;
  c.out = m.duration;
  c.start = start >= 0 ? p.frame(start) : track_end(p, track);
  // Never overlap: move to the end of the track if the place is taken.
  if (!range_free(p, track, c.start, c.end())) c.start = track_end(p, track);
  p.clips.push_back(c);
  return c.id;
}

// Splits the clip under `t` on every unlocked track, or only clip `id`.
inline int split(Project &p, double t, int id = 0) {
  t = p.frame(t);
  int count = 0;
  const size_t n = p.clips.size();
  for (size_t i = 0; i < n; ++i) {
    Clip c = p.clips[i];
    if (id && c.id != id) continue;
    if (p.tracks[size_t(c.track)].locked) continue;
    if (t <= c.start + 1e-6 || t >= c.end() - 1e-6) continue;
    Clip right = c;
    right.id = p.next_clip_id++;
    right.in = c.in + (t - c.start);
    right.start = t;
    p.clips[i].out = right.in;
    p.clips.push_back(right);
    ++count;
  }
  return count;
}

inline void remove_clip(Project &p, int id) {
  std::erase_if(p.clips, [&](const Clip &c) { return c.id == id; });
}

// Removes the clip and closes the gap it leaves on its track.
inline void ripple_delete(Project &p, int id) {
  const Clip *c = p.find(id);
  if (!c) return;
  const int track = c->track;
  const double start = c->start, length = c->length();
  remove_clip(p, id);
  for (Clip &other : p.clips)
    if (other.track == track && other.start >= start - 1e-6) other.start -= length;
}

} // namespace cut
