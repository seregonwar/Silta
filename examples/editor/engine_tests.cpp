#include "engine.hpp"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#ifdef _WIN32
#include <objbase.h>
#endif

using namespace cut;
void require(bool ok, const char *message) { if (!ok) throw std::runtime_error(message); }
double energy(const std::vector<float> &samples) { double e=0; for (float s:samples) e+=s*s; return e; }
void wait_export(Export &e) {
  auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(40);
  while (!e.finished() && std::chrono::steady_clock::now()<deadline) std::this_thread::sleep_for(std::chrono::milliseconds(10));
  require(e.finished(),"export timeout");
}
int main(int argc,char **argv) {
  try {
    require(argc==3,"usage: cut_engine_test source-video output-new.mp4");
    Project p; auto m=probe(argv[1]); require(m && m->has_audio && m->has_video,"AV input");
    p.media.push_back(*m); p.width=320; p.height=180;
    int id=add_clip(p,0); p.find(id)->in=0.5; p.find(id)->out=1.5;
    Renderer r;
    auto original=r.video(p,0.2,320,180);
    p.find(id)->opacity=0.5f;
    auto half=r.video(p,0.2,320,180);
    for (size_t i=0;i<original.rgba.size();++i) if (i%4!=3)
      require(std::abs(int(half.rgba[i])*2-int(original.rgba[i]))<=1,"opacity compositing");
    p.tracks[1].hidden=true;
    auto black=r.video(p,0.2,320,180);
    for (size_t i=0;i<black.rgba.size();++i) if (i%4!=3) require(black.rgba[i]==0,"hidden video");
    p.tracks[1].hidden=false;
    std::vector<float> full(4800*2),quiet(4800*2);
    r.audio(p,0.1,full.data(),4800); p.find(id)->volume=0.5f;
    r.audio(p,0.1,quiet.data(),4800);
    require(energy(full)>0.001 && std::abs(energy(quiet)/energy(full)-0.25)<0.01,"volume mixer");
    p.tracks[1].muted=true; r.audio(p,0.1,quiet.data(),4800); require(energy(quiet)==0,"muted audio");
    p.tracks[1].muted=false;
    // A gap followed by a trimmed AV clip, and a second overlapping audio track.
    p.find(id)->start=0.2;
    int second=add_clip(p,0,2,0); p.find(second)->out=1.2; p.find(second)->volume=0.25f;
    r.audio(p,1.3,quiet.data(),4800); require(energy(quiet)==0,"silence after timeline");
    {
      Playback playback; playback.set_project(p); playback.seek(0.3);
#ifdef _WIN32
      APTTYPE apartment; APTTYPEQUALIFIER qualifier;
      require(SUCCEEDED(CoGetApartmentType(&apartment, &qualifier)) &&
        (apartment == APTTYPE_STA || apartment == APTTYPE_MAINSTA),
        "playback must preserve STA for native file dialogs");
#endif
      require(std::abs(playback.position()-0.3)<1e-6,"seek position");
      playback.toggle(); std::this_thread::sleep_for(std::chrono::milliseconds(550)); playback.toggle();
      require(playback.position()>0.45 && playback.position()<1.1,"playback clock");
      Picture pic; uint64_t version=0; require(playback.picture(pic,version) && pic.valid(),"worker preview");
      std::cout<<"Playback clock: "<<(playback.audio_device()?"audio":"fallback")<<"\n";
    }
    Export e; e.start(p,argv[2]); wait_export(e); require(e.error().empty(),e.error().c_str());
    auto output=probe(argv[2]);
    require(output && output->has_audio && output->has_video && output->width==320 && output->height==180,"MP4 streams");
    require(std::abs(output->duration-p.duration())<0.05,"export duration");
    VideoDecoder d; require(d.open(argv[2],320,180),"export video open");
    Picture pic; require(d.frame_at(0,pic),"export decode");
    for (size_t i=0;i<pic.rgba.size();++i) if (i%4!=3) require(pic.rgba[i]<5,"export gap is black");
    e.start(p,argv[2]); wait_export(e); require(!e.error().empty(),"existing output protection");
    std::string cancelled=std::string(argv[2])+".cancel.mp4";
    e.start(p,cancelled); e.cancel(); wait_export(e);
    require(e.error()=="Export cancelled" && !std::filesystem::exists(cancelled) &&
      !std::filesystem::exists(cancelled+".partial.mp4"),"cancel cleanup");
    std::cout<<"Compositor, mixer, playback, MP4, overwrite and cancellation tests passed\n";
    return 0;
  } catch (const std::exception &e) { std::cerr<<e.what()<<'\n'; return 1; }
}
