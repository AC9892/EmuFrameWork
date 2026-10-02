#include "../../src/core/backend.hpp"
#include <mgba/core/core.h>
#include <mgba/core/serialize.h>
#include <mgba-util/audio-buffer.h>
#include <mgba-util/vfs.h>
#include <algorithm>
#include <array>
#include <fcntl.h>
#include <string>
namespace ef {
namespace {
std::string utf8(const std::filesystem::path& p){auto s=p.u8string();return std::string(s.begin(),s.end());}
class MGBABackend final : public Backend {
  mCore* core_=nullptr;
  std::array<uint32_t,256*224> pixels_{};
  EF_VideoFrame frame_{}, empty_{};
public:
  ~MGBABackend() override { if(core_){core_->unloadROM(core_);core_->deinit(core_);} }
  bool load(const std::filesystem::path& rom,const std::filesystem::path& save) override {
    auto rp=utf8(rom),sp=utf8(save);
    core_=mCoreFind(rp.c_str());if(!core_)return false;
    mCoreInitConfig(core_,"EmuFrame");
    if(!core_->init(core_)){core_=nullptr;return false;}
    core_->setVideoBuffer(core_,pixels_.data(),256);
    core_->setAudioBufferSize(core_,4096);
    if(!mCoreLoadFile(core_,rp.c_str()))return false;
    if(!mCoreLoadSaveFile(core_,sp.c_str(),false))return false;
    core_->reset(core_);
    unsigned w=0,h=0;core_->currentVideoSize(core_,&w,&h);
    frame_={w,h,256*4,EF_PIXEL_RGBA8888,0};return w>0&&h>0&&w<=256&&h<=224;
  }
  void reset() override {core_->reset(core_);frame_.sequence++;}
  void run_frame() override {core_->runFrame(core_);unsigned w=0,h=0;core_->currentVideoSize(core_,&w,&h);frame_.width=w;frame_.height=h;frame_.sequence++;}
  void set_input(uint32_t bits) override {core_->setKeys(core_,bits&0x3ffu);}
  EF_VideoFrame video_info() const override {return frame_;}
  const uint32_t* video_pixels() const override {return pixels_.data();}
  EF_AudioInfo audio_info() const override {auto* a=core_->getAudioBuffer(core_);return {core_->audioSampleRate(core_),a?a->channels:0u,a?mAudioBufferAvailable(a):0u};}
  size_t read_audio(int16_t* out,size_t frames) override {auto* a=core_->getAudioBuffer(core_);return a?mAudioBufferRead(a,out,frames):0;}
  bool save_state(const std::filesystem::path& path) override {auto p=utf8(path);auto* f=VFileOpen(p.c_str(),O_CREAT|O_TRUNC|O_RDWR);if(!f)return false;bool ok=mCoreSaveStateNamed(core_,f,SAVESTATE_RTC);f->close(f);return ok;}
  bool load_state(const std::filesystem::path& path) override {auto p=utf8(path);auto* f=VFileOpen(p.c_str(),O_RDONLY);if(!f)return false;bool ok=mCoreLoadStateNamed(core_,f,SAVESTATE_RTC);f->close(f);return ok;}
  const char* name() const override {return "mGBA";}
};
}
std::unique_ptr<Backend> create_mgba_backend(){return std::make_unique<MGBABackend>();}
}
