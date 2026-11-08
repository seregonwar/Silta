// Silta Cut: a small native multitrack editor. All visible controls use Silta;
// ImGui supplies windows, hit targets and the deterministic input harness.
#include "engine.hpp"
#include "silta/app.hpp"
#include "silta/silta.hpp"
#include <cstdio>
#include <sstream>
#include <filesystem>
#include <chrono>

using namespace silta;
using namespace cut;
namespace {
const Color bg = Color::hex(0x101114), panel = Color::hex(0x18191e), line = Color::hex(0x2a2c33);
const Color ink = Color::hex(0xe8e9ef), muted = Color::hex(0x9195a5), accent = Color::hex(0xb5a2ff);
ImRect box(float x, float y, float w, float h) { return {{px(x), px(y)}, {px(x+w), px(y+h)}}; }
void fill(const ImRect &r, Color c, float rounding = 0) { draw::fill(ImGui::GetWindowDrawList(), r, c, px(rounding)); }
void label(float x, float y, const std::string &s, Color c = ink, TextSize size = TextSize::sm) {
  draw_text(ImGui::GetWindowDrawList(), {px(x), px(y)}, s.c_str(), size, Weight::regular, c);
}
bool hit(const char *id, const ImRect &r) {
  ImGui::SetCursorScreenPos(r.Min); return ImGui::InvisibleButton(id, r.GetSize());
}
bool button(float x, float y, float w, const char *text, Icon icon = Icon::none,
            bool disabled = false, Variant variant = Variant::ghost) {
  ImGui::SetCursorScreenPos({px(x), px(y)});
  layout::Isolate isolate;
  return Button(text, {.variant = variant, .size = Size::sm, .icon = icon, .disabled = disabled, .width = w});
}
std::string timecode(double t, double fps = 30) {
  int frames = std::max(0, int(std::round(t*fps))), rate = int(std::round(fps));
  char text[64]; std::snprintf(text, sizeof text, "%02d:%02d:%02d:%02d", frames/rate/3600,
    frames/rate/60%60, frames/rate%60, frames%rate); return text;
}
struct Job {
  Analysis analysis;
  std::atomic<bool> cancel{false};
  std::thread thread;
  std::vector<Texture> textures;
  explicit Job(MediaInfo m) : thread([this, m] { analyze(m, analysis, cancel); }) {}
  ~Job() { cancel = true; if (thread.joinable()) thread.join(); }
  void upload() {
    if (!analysis.done || !textures.empty()) return;
    for (auto &p : analysis.thumbs) { Texture t; t.update(p.rgba.data(), p.width, p.height); textures.push_back(std::move(t)); }
  }
};
struct Editor {
  Project project;
  History history;
  Playback player;
  Export exporter;
  std::map<std::string, std::unique_ptr<Job>> jobs;
  Texture preview;
  uint64_t preview_version = 0;
  int selected = 0, bin_drag = -1, tool = 0;
  float zoom = 80, scroll = 0, bin_scroll = 0;
  bool snap = true, export_seen = true;
  std::string export_path;
  struct Drag { int id = 0, mode = 0; float mouse_x = 0; Clip original; Project before; } drag;
  Project slider_before;
  float w = 1440, h = 900, timeline_y = 540;
  int exit_code = 0;
  std::vector<std::string> script;
  size_t script_step = 0;
  int wait = 10;
  double until = 0, await_started = 0;

  void changed() { player.set_project(project); request_frames(2); }
  void record() { history.record(project); }
  void undo(bool redo = false) { if (redo ? history.redo(project) : history.undo(project)) { selected = 0; changed(); } }
  void import(const std::string &path, bool append = false) {
    std::string error;
    auto info = probe(path, &error);
    if (!info) { toast("Cannot import file", {.description = error.c_str(), .kind = ToastKind::error}); return; }
    int index = -1;
    for (int i = 0; i < int(project.media.size()); ++i) if (project.media[i].path == path) index = i;
    record();
    if (index < 0) {
      index = int(project.media.size()); project.media.push_back(*info);
      if (!jobs.contains(path)) jobs[path] = std::make_unique<Job>(*info);
    }
    if (append) selected = add_clip(project, index);
    changed();
  }
  void import_dialog() {
    const FileFilter filters[] = {{"Media", "*.mp4;*.mov;*.mkv;*.webm;*.avi;*.m4a;*.mp3;*.wav;*.flac;*.png;*.jpg"}};
    for (auto &path : open_file_dialog(filters, true, "Import media")) import(path);
  }
  void append(int media, int track = -1, double start = -1) {
    if (media < 0 || media >= int(project.media.size())) return;
    if (track >= 0 && project.tracks[track].locked) return;
    record(); selected = add_clip(project, media, track, start); changed();
  }
  void split_here() {
    Project before = project;
    if (split(project, player.position(), selected)) { history.record(before); changed(); }
  }
  void erase(bool ripple) {
    auto *c = project.find(selected);
    if (!c || project.tracks[c->track].locked) return;
    record(); if (ripple) ripple_delete(project, selected); else remove_clip(project, selected);
    selected = 0; changed();
  }
  void start_export(const std::string &path) {
    if (exporter.running()) return;
    export_path = path; export_seen = false; exporter.start(project, path); OpenDialog("export");
  }
  void export_dialog() {
    const FileFilter filters[] = {{"MP4 video", "*.mp4"}};
    if (auto path = save_file_dialog(filters, "Silta Cut.mp4", "Export MP4 (choose a new filename)")) start_export(*path);
  }
  bool all_analyzed() {
    for (auto &[path, job] : jobs) if (!job->analysis.done) return false;
    return true;
  }
  void shortcuts() {
    auto &io = ImGui::GetIO();
    if (io.WantTextInput || ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)) return;
    if (ImGui::IsKeyPressed(ImGuiKey_Space, false)) player.toggle();
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z, false)) undo(io.KeyShift);
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y, false)) undo(true);
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_O, false)) import_dialog();
    if (ImGui::IsKeyPressed(ImGuiKey_Delete, false)) erase(io.KeyShift);
    if (!io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false)) split_here();
    if (ImGui::IsKeyPressed(ImGuiKey_B, false)) tool = 1;
    if (ImGui::IsKeyPressed(ImGuiKey_V, false)) tool = 0;
    if (ImGui::IsKeyPressed(ImGuiKey_N, false)) snap = !snap;
    if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) player.seek(player.position()-1/project.fps);
    if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) player.seek(player.position()+1/project.fps);
    if (ImGui::IsKeyPressed(ImGuiKey_Home, false)) player.seek(0);
  }
  void top() {
    fill(box(0, 0, w, 64), panel);
    fill(box(20, 17, 30, 30), accent, 7);
    draw_icon(ImGui::GetWindowDrawList(), Icon::scissors, {px(25),px(22)},px(20),bg);
    label(62, 13, "Silta Cut", ink, TextSize::lg);
    label(62, 37, "MAKE THE CUT.", muted, TextSize::xs);
    label(220, 24, project.name, muted);
    if (button(w-390, 16, 44, "##undo", Icon::undo, !history.can_undo())) undo();
    Tooltip("Undo · Ctrl+Z");
    if (button(w-340, 16, 44, "##redo", Icon::redo, !history.can_redo())) undo(true);
    Tooltip("Redo · Ctrl+Shift+Z");
    if (button(w-278, 16, 125, "Import media", Icon::plus, false, Variant::outline)) import_dialog();
    if (button(w-138, 16, 118, "Export MP4", Icon::upload, project.clips.empty() || exporter.running(), Variant::primary)) export_dialog();
  }
  void media_bin() {
    fill(box(0, 65, 268, timeline_y-65), panel);
    label(20, 82, "Media library", ink);
    label(20, 110, std::to_string(project.media.size())+" assets  /  double-click to add", muted, TextSize::xs);
    auto area = box(8, 140, 252, timeline_y-195);
    if (area.Contains(ImGui::GetMousePos()) && ImGui::GetIO().MouseWheel != 0)
      bin_scroll = std::clamp(bin_scroll-ImGui::GetIO().MouseWheel*60, 0.f,
        std::max(0.f, float(project.media.size())*88-area.GetHeight()/scale()));
    auto *list = ImGui::GetWindowDrawList(); list->PushClipRect(area.Min, area.Max, true);
    for (int i = 0; i < int(project.media.size()); ++i) {
      const auto &m = project.media[i]; float y = 142+i*88-bin_scroll;
      auto r = box(14, y, 240, 78);
      if (!r.Overlaps(area)) continue;
      ImGui::PushID(i);
      hit("asset", r);
      bool hovered = ImGui::IsItemHovered();
      fill(r, hovered ? Color::hex(0x2b2c35) : Color::hex(0x202127), 7);
      if (hovered && ImGui::IsMouseClicked(0)) bin_drag = i;
      if (hovered && ImGui::IsMouseDoubleClicked(0)) { append(i); bin_drag = -1; }
      auto &job = *jobs[m.path];
      if (!job.textures.empty()) draw_image(list, job.textures.front(), box(22,y+9,82,46), Fit::cover, px(4));
      else draw_icon(list, m.has_video ? Icon::film : Icon::music, {px(45),px(y+20)}, px(24), accent);
      list->PushClipRect(box(114,y,130,78).Min,box(114,y,130,78).Max,true);
      label(114,y+11,m.name,ink,TextSize::xs);
      label(114,y+31,timecode(m.duration),muted,TextSize::xs);
      label(114,y+51,m.has_video ? std::to_string(m.width)+" × "+std::to_string(m.height) : "Audio · stereo",muted,TextSize::xs);
      list->PopClipRect(); ImGui::PopID();
    }
    if (project.media.empty()) { label(25, 190,"Your story starts here.",ink); label(25,220,"Import or drop media files",muted,TextSize::xs); }
    list->PopClipRect();
    label(20, timeline_y-36, "MP4 · MOV · WAV · MP3 · images", muted, TextSize::xs);
  }
  void monitor() {
    float left = 284, right = w-284, width = right-left;
    label(left, 82, "Program monitor", ink);
    label(right-142, 84, "1920 × 1080  /  30 fps", muted, TextSize::xs);
    auto r = box(left, 118, width, timeline_y-200);
    fill(r, black, 6);
    if (preview.valid()) draw_image(ImGui::GetWindowDrawList(),preview,r,Fit::contain);
    else label(left+width/2-70, 250, "Preview is loading", muted);
    float cy = timeline_y-66, mid = (left+right)/2;
    if (button(mid-74,cy,38,"##home",Icon::skip_back)) player.seek(0);
    if (button(mid-24,cy,48,"##play",player.playing()?Icon::pause:Icon::play,false,Variant::secondary)) player.toggle();
    Tooltip("Play / pause · Space");
    if (button(mid+36,cy,38,"##end",Icon::skip_forward)) player.seek(project.duration());
    label(left,cy+6,timecode(player.position(),project.fps),accent);
    label(right-112,cy+6,timecode(project.duration(),project.fps),muted);
  }
  void inspector_slider(float y, const char *id, float &value, float maximum) {
    Project before = project;
    ImGui::SetCursorScreenPos({px(w-242),px(y)});
    layout::Isolate isolate;
    bool edited = Slider(id,&value,0,maximum,{.step=0.01f,.width=214});
    if (ImGui::IsItemActivated()) slider_before = std::move(before);
    if (edited) changed();
    if (ImGui::IsItemDeactivatedAfterEdit()) history.record(slider_before);
  }
  void inspector() {
    fill(box(w-268,65,268,timeline_y-65),panel);
    label(w-244,82,"Inspector");
    auto *c = project.find(selected);
    if (!c) { label(w-244,145,"Nothing selected",muted); label(w-244,175,"Select a clip in the timeline",muted,TextSize::xs); return; }
    const auto &m = project.media[c->media];
    label(w-244,126,m.name,accent);
    label(w-244,163,"TIMING",muted,TextSize::xs);
    label(w-244,194,"Start     "+timecode(c->start),ink,TextSize::xs);
    label(w-244,222,"Source   "+timecode(c->in)+"  →  "+timecode(c->out),muted,TextSize::xs);
    label(w-244,250,"Length   "+timecode(c->length()),ink,TextSize::xs);
    if (project.tracks[c->track].locked) { label(w-244,302,"Unlock track to edit",muted); return; }
    label(w-244,296,"Volume     "+std::to_string(int(c->volume*100))+"%",muted,TextSize::xs);
    inspector_slider(323,"volume",c->volume,2);
    if (m.has_video) { label(w-244,364,"Opacity     "+std::to_string(int(c->opacity*100))+"%",muted,TextSize::xs); inspector_slider(391,"opacity",c->opacity,1); }
  }
  double snap_time(double t, int except) {
    t = project.frame(std::max(0.0,t));
    if (!snap) return t;
    double best = t, distance = 8/zoom;
    auto near = [&](double v) { if (v >= 0 && std::abs(t-v)<distance) { best=v; distance=std::abs(t-v); } };
    near(0); near(player.position());
    for (auto &c : project.clips) if (c.id != except) { near(c.start); near(c.end()); }
    return project.frame(best);
  }
  void timeline() {
    float ty = timeline_y, origin = 160, right = w-20;
    fill(box(0,ty,w,h-ty),panel);
    label(20,ty+15,"Timeline");
    if (button(160,ty+9,38,"##select",Icon::pointer,false,tool==0?Variant::secondary:Variant::ghost)) tool=0;
    Tooltip("Selection · V");
    if (button(202,ty+9,38,"##blade",Icon::scissors,false,tool==1?Variant::secondary:Variant::ghost)) tool=1;
    Tooltip("Blade · B");
    if (button(252,ty+9,38,"##snap",Icon::magnet,false,snap?Variant::secondary:Variant::ghost)) snap=!snap;
    Tooltip("Snap · N");
    if (button(306,ty+9,76,"Split",Icon::scissors,project.clips.empty())) split_here();
    if (button(388,ty+9,128,"Ripple delete",Icon::trash,!project.find(selected))) erase(true);
    label(w-285,ty+18,"Zoom",muted,TextSize::xs);
    ImGui::SetCursorScreenPos({px(w-230),px(ty+17)});
    { layout::Isolate isolate; Slider("zoom",&zoom,20,240,{.width=190}); }
    auto region = box(origin,ty+50,right-origin,h-ty-85);
    bool over = region.Contains(ImGui::GetMousePos());
    auto &io = ImGui::GetIO();
    if (over && io.MouseWheel != 0) {
      if (io.KeyCtrl) zoom = std::clamp(zoom*std::pow(1.15f,io.MouseWheel),20.f,240.f);
      else scroll = std::max(0.f,scroll-io.MouseWheel*80/zoom);
    }
    scroll = std::clamp(scroll,0.f,float(std::max(0.0,project.duration()+5-(right-origin)/zoom)));
    auto tx = [&](double t) { return origin+float(t-scroll)*zoom; };
    auto at = [&](float x) { return std::max(0.0,double((x-origin)/zoom+scroll)); };
    float ruler_y=ty+50, tracks_y=ty+84, track_h=(h-tracks_y-36)/4;
    auto ruler = box(origin,ruler_y,right-origin,32);
    hit("ruler",ruler);
    if (ImGui::IsItemActive() && ImGui::IsMouseDown(0)) player.seek(project.frame(at(io.MousePos.x/scale())));
    auto *list = ImGui::GetWindowDrawList();
    list->PushClipRect(region.Min,region.Max,true);
    double tick = zoom < 40 ? 5 : zoom < 100 ? 2 : 1;
    for (double t = std::floor(scroll/tick)*tick; tx(t) < right; t+=tick) {
      float x=tx(t); list->AddLine({px(x),px(ruler_y+23)},{px(x),px(h-36)},line.u32());
      label(x+5,ruler_y+2,std::to_string(int(t))+"s",muted,TextSize::xs);
    }
    list->PopClipRect();
    for (int tr=0; tr<4; ++tr) {
      float y=tracks_y+tr*track_h;
      fill(box(0,y,w,track_h-1),tr%2?Color::hex(0x17181d):Color::hex(0x1c1d23));
      auto &track=project.tracks[tr];
      label(16,y+9,track.name,muted,TextSize::xs);
      ImGui::PushID(tr);
      if (button(12,y+32,34,"##lock",track.locked?Icon::lock:Icon::unlock)) { record(); track.locked=!track.locked; changed(); }
      if (button(52,y+32,34,"##mute",track.muted?Icon::volume_off:Icon::volume)) { record(); track.muted=!track.muted; changed(); }
      if (track.kind==TrackKind::video && button(92,y+32,34,"##hide",track.hidden?Icon::eye_off:Icon::eye)) { record(); track.hidden=!track.hidden; changed(); }
      ImGui::PopID();
    }
    list->PushClipRect(region.Min,region.Max,true);
    for (const Clip c : project.clips) {
      float y=tracks_y+c.track*track_h+6, x=tx(c.start), cw=float(c.length())*zoom;
      auto r=box(x,y,std::max(3.f,cw),track_h-13);
      if (!r.Overlaps(region)) continue;
      const auto &m=project.media[c.media]; bool audio=project.tracks[c.track].kind==TrackKind::audio;
      ImGui::PushID(c.id);
      // Clamp the hit target too: invisible clips must never intercept track headers.
      ImRect target=r; target.ClipWith(region); hit("clip",target);
      if (ImGui::IsItemHovered()) {
        if (ImGui::IsMouseClicked(0)) {
          selected=c.id;
          if (!project.tracks[c.track].locked) {
            if (tool==1) { record(); split(project,at(io.MousePos.x/scale()),c.id); changed(); ImGui::PopID(); break; }
            float mx=io.MousePos.x/scale();
            drag={c.id,mx<x+8?1:mx>x+cw-8?2:0,mx,c,project};
          }
        }
        Tooltip("Drag to move · drag edges to trim");
      }
      Color clip_color=audio?Color::hex(0x22564e):Color::hex(0x443966);
      fill(r,clip_color,5);
      list->PushClipRect(r.Min,r.Max,true);
      auto &job=*jobs[m.path];
      if (!audio && !job.textures.empty()) {
        float image_h=std::max(1.f,track_h-42), tw=image_h*16/9;
        for (float dx=0; dx<cw; dx+=tw) {
          size_t index=std::min(job.textures.size()-1,size_t((c.in+double(dx)/zoom)/job.analysis.thumb_interval));
          draw_image(list,job.textures[index],box(x+dx,y+25,tw,image_h),Fit::cover);
        }
      }
      if (m.has_audio && job.analysis.done && !job.analysis.peaks.empty()) {
        const auto &peaks=job.analysis.peaks;
        float center=audio?y+(track_h-13)*0.62f:y+track_h-22, amp=audio?track_h*0.24f:8.f;
        float start=std::max(0.f,origin-x), end=std::min(cw,right-x);
        for (float dx=start; dx<end; dx+=3) {
          size_t index=size_t(std::max(0.0,(c.in+dx/zoom)*100));
          float a=index<peaks.size()?peaks[index]*amp:0;
          list->AddLine({px(x+dx),px(center-a)},{px(x+dx),px(center+a)},Color::hex(0x8ae0c5).u32());
        }
      }
      fill(box(x,y,cw,24),clip_color);
      label(x+9,y+3,m.name,ink,TextSize::xs);
      list->PopClipRect();
      draw::border(list,r,selected==c.id?accent:clip_color,px(5),px(selected==c.id?2.f:1.f));
      if (selected==c.id) { fill(box(x+3,y+30,2,12),accent,1); fill(box(x+cw-5,y+30,2,12),accent,1); }
      ImGui::PopID();
    }
    float play_x=tx(player.position());
    list->AddLine({px(play_x),px(ruler_y+22)},{px(play_x),px(h-36)},accent.u32(),px(2));
    list->AddTriangleFilled({px(play_x-6),px(ruler_y+16)},{px(play_x+6),px(ruler_y+16)},{px(play_x),px(ruler_y+24)},accent.u32());
    list->PopClipRect();
    if (drag.id && ImGui::IsMouseDown(0)) {
      auto *c=project.find(drag.id);
      if (c) {
        Clip candidate=drag.original;
        double delta=(io.MousePos.x/scale()-drag.mouse_x)/zoom;
        if (drag.mode==0) {
          double raw=std::max(0.0,candidate.start+delta), start=snap_time(raw,c->id);
          // Snap either edge of a moving clip.
          double end=snap_time(raw+candidate.length(),c->id);
          if (snap && std::abs(end-raw-candidate.length())<std::abs(start-raw)) start=end-candidate.length();
          candidate.start=std::max(0.0,project.frame(start));
          int tr=int((io.MousePos.y/scale()-tracks_y)/track_h);
          if (tr>=0 && tr<4 && !project.tracks[tr].locked &&
              (project.tracks[tr].kind==TrackKind::video?m_has_video(candidate):project.media[candidate.media].has_audio)) candidate.track=tr;
        } else if (drag.mode==1) {
          double start=std::clamp(snap_time(candidate.start+delta,c->id),std::max(0.0,candidate.start-candidate.in),candidate.end()-1/project.fps);
          candidate.in+=start-candidate.start; candidate.start=start;
        } else {
          double end=std::clamp(snap_time(candidate.end()+delta,c->id),candidate.start+1/project.fps,
            candidate.start+project.media[candidate.media].duration-candidate.in);
          candidate.out=candidate.in+end-candidate.start;
        }
        if (range_free(project,candidate.track,candidate.start,candidate.end(),candidate.id)) { *c=candidate; changed(); }
      }
    }
    if (drag.id && ImGui::IsMouseReleased(0)) {
      auto *c=project.find(drag.id);
      if (c && (c->start!=drag.original.start || c->in!=drag.original.in || c->out!=drag.original.out || c->track!=drag.original.track)) history.record(drag.before);
      drag.id=0;
    }
    if (bin_drag>=0 && ImGui::IsMouseReleased(0)) {
      int tr=int((io.MousePos.y/scale()-tracks_y)/track_h);
      if (over && tr>=0 && tr<4 && !project.tracks[tr].locked) {
        const auto &m=project.media[bin_drag];
        if (project.tracks[tr].kind==TrackKind::video?m.has_video:m.has_audio)
          append(bin_drag,tr,snap_time(at(io.MousePos.x/scale()),0));
      }
      bin_drag=-1;
    }
    label(18,h-27,"V Select    B Blade    S Split    Shift+Del Ripple delete    Space Play",muted,TextSize::xs);
    label(w-330,h-27,player.audio_device()?"48 kHz stereo  ·  audio clock":"No audio device · silent preview",muted,TextSize::xs);
  }
  bool m_has_video(const Clip &c) { return project.media[c.media].has_video; }
  void frame(App &app) {
    w=ImGui::GetIO().DisplaySize.x/scale(); h=ImGui::GetIO().DisplaySize.y/scale();
    timeline_y=std::max(455.f,h*0.57f);
    ImGui::SetNextWindowPos({0,0}); ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    ImGui::Begin("Silta Cut##editor",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoMove|
      ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoScrollWithMouse|ImGuiWindowFlags_NoScrollbar);
    fill(box(0,0,w,h),bg);
    for (auto &path : app.take_dropped_files()) import(path);
    for (auto &[path,job] : jobs) job->upload();
    Picture picture;
    if (player.picture(picture,preview_version) && picture.valid()) preview.update(picture.rgba.data(),picture.width,picture.height);
    shortcuts(); top(); media_bin(); monitor(); inspector(); timeline();
    if (exporter.finished() && !export_seen) {
      export_seen=true;
      auto error=exporter.error();
      toast(error.empty()?"Export complete":"Export stopped",{.description=error.empty()?export_path.c_str():error.c_str(),
        .kind=error.empty()?ToastKind::success:ToastKind::error});
    }
    if (auto dlg=Dialog("export",{.title="Export MP4",.description="H.264 video · AAC audio · 1080p / 30 fps",
                                  .width=470,.close_button=false,.dismissable=false})) {
      Text(export_path.c_str(),{.size=TextSize::xs,.wrap=true,.muted=true}); Spacer(18);
      Progress("encode",exporter.progress()); Spacer(12);
      std::string status=exporter.running()?"Rendering your timeline… "+std::to_string(int(exporter.progress()*100))+"%":
        exporter.error().empty()?"Your video is ready.":exporter.error();
      Text(status.c_str(),{.wrap=true}); Spacer(18);
      auto footer=DialogFooter();
      if (exporter.running()) { if (Button("Cancel",{.variant=Variant::outline})) exporter.cancel(); }
      else if (Button("Done")) CloseDialog();
    }
    Toaster();
    run_script(app); // OpenDialog must use the same window ID scope as Dialog.
    ImGui::End();
    // Poll worker completions without touching ImGui from worker threads.
    wake_in(1.0/60);
  }
  void fail(App &app,const std::string &message) {
    std::fprintf(stderr,"%s\n",message.c_str()); exit_code=2; app.close();
  }
  void run_script(App &app) {
    if (script_step>=script.size()) return;
    request_frames(2);
    if (wait>0) { --wait; return; }
    if (ImGui::GetTime()<until) return;
    std::string cmd=script[script_step]; size_t colon=cmd.find(':');
    std::string verb=cmd.substr(0,colon),arg=colon==std::string::npos?"":cmd.substr(colon+1);
    auto &io=ImGui::GetIO();
    if (verb=="await") {
      bool ready=arg=="analysis"?all_analyzed():arg=="preview"?preview.valid():arg=="export"?exporter.finished():false;
      if (!ready) { if (!await_started) await_started=ImGui::GetTime();
        if (ImGui::GetTime()-await_started>90) fail(app,"Timeout: "+cmd); return; }
      await_started=0;
    }
    ++script_step; wait=4;
    auto point=[&] { size_t comma=arg.find(','); return ImVec2(px(std::stof(arg.substr(0,comma))),px(std::stof(arg.substr(comma+1)))); };
    if (verb=="move" || verb=="click" || verb=="down" || verb=="up") {
      auto p=point(); io.AddMousePosEvent(p.x,p.y);
      if (verb=="click" || verb=="down") io.AddMouseButtonEvent(0,true);
      if (verb=="click" || verb=="up") io.AddMouseButtonEvent(0,false);
    } else if (verb=="key") {
      ImGuiKey key=arg=="space"?ImGuiKey_Space:arg=="s"?ImGuiKey_S:arg=="b"?ImGuiKey_B:arg=="v"?ImGuiKey_V:
        arg=="delete"?ImGuiKey_Delete:arg=="home"?ImGuiKey_Home:arg=="right"?ImGuiKey_RightArrow:
        arg=="undo"?ImGuiKey_Z:arg=="redo"?ImGuiKey_Y:ImGuiKey_Escape;
      bool ctrl=arg=="undo" || arg=="redo";
      if (ctrl) io.AddKeyEvent(ImGuiMod_Ctrl,true);
      io.AddKeyEvent(key,true); io.AddKeyEvent(key,false);
      if (ctrl) io.AddKeyEvent(ImGuiMod_Ctrl,false);
    } else if (verb=="wait") wait=std::stoi(arg);
    else if (verb=="sleep") until=ImGui::GetTime()+std::stod(arg)/1000;
    else if (verb=="seek") player.seek(std::stod(arg));
    else if (verb=="export") start_export(arg);
    else if (verb=="shot") app.screenshot(arg.c_str());
    else if (verb=="check") {
      size_t eq=arg.find('='); std::string field=arg.substr(0,eq); double expected=std::stod(arg.substr(eq+1));
      double value=field=="clips"?double(project.clips.size()):field=="duration"?project.duration():
        field=="position"?player.position():field=="selected"?selected:field=="export"?(exporter.finished()&&exporter.error().empty()?1:0):
        field=="audio"?(player.audio_device()?1:0):field=="start"?(project.find(selected)?project.find(selected)->start:-1):
        field=="in"?(project.find(selected)?project.find(selected)->in:-1):field=="out"?(project.find(selected)?project.find(selected)->out:-1):
        field=="track"?(project.find(selected)?project.find(selected)->track:-1):field=="volume"?(project.find(selected)?project.find(selected)->volume:-1):
        field=="opacity"?(project.find(selected)?project.find(selected)->opacity:-1):field=="zoom"?zoom:
        field=="dialog"?(ImGui::IsPopupOpen("export")?1:0):-999;
      // Slider pixel rounding varies slightly between hosted-runner DPI scales.
      const double tolerance = field=="zoom" ? 1.0 : 0.04;
      if (std::abs(value-expected)>tolerance) fail(app,"Failed "+arg+" actual="+std::to_string(value));
    } else if (verb=="quit") app.close();
  }
};
}
int main(int argc,char **argv) {
  bool hidden=false; Backend backend=Backend::automatic; std::vector<std::string> imports,script;
  for (int i=1;i<argc;++i) {
    std::string a=argv[i];
    if (a=="--hidden") hidden=true;
    else if (a=="--backend=vulkan") backend=Backend::vulkan;
    else if (a=="--backend=d3d11") backend=Backend::d3d11;
    else if (a.starts_with("--import=")) imports.push_back(a.substr(9));
    else if (a.starts_with("--script=")) { std::stringstream ss(a.substr(9)); std::string cmd; while (std::getline(ss,cmd,';')) script.push_back(cmd); }
  }
  App app({.title="Silta Cut",.width=1440,.height=900,.backend=backend,
    .init={.theme=Theme::dark(BaseColor::zinc)},.root_window=false,.hidden=hidden});
  if (!app.ok()) return 1;
  Editor editor; editor.script=std::move(script);
  for (auto &path:imports) editor.import(path,true);
  editor.changed();
  int result=app.run([&] { editor.frame(app); });
  return editor.exit_code?editor.exit_code:result;
}
