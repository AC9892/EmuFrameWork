#include "../../src/core/backend.hpp"
#include <mgba/core/core.h>
#include <mgba/core/serialize.h>
#include <mgba-util/audio-buffer.h>
#include <mgba-util/audio-resampler.h>
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
  mAudioBuffer output_audio_{};
  mAudioResampler resampler_{};
  bool audio_ready_=false;
  std::array<uint32_t,256*224> pixels_{};
  EF_VideoFrame frame_{}, empty_{};
  static constexpr unsigned output_rate_=44100;
  void reset_audio(){
    if(!audio_ready_)return;
    mAudioBufferClear(&output_audio_);
    mAudioBufferClear(core_->getAudioBuffer(core_));
    mAudioResamplerDeinit(&resampler_);
    mAudioResamplerInit(&resampler_,mINTERPOLATOR_SINC);
    mAudioResamplerSetSource(&resampler_,core_->getAudioBuffer(core_),core_->audioSampleRate(core_),true);
    mAudioResamplerSetDestination(&resampler_,&output_audio_,output_rate_);
  }
public:
  ~MGBABackend() override {if(audio_ready_){mAudioResamplerDeinit(&resampler_);mAudioBufferDeinit(&output_audio_);}if(core_){core_->unloadROM(core_);core_->deinit(core_);} }
  bool load(const std::filesystem::path& rom,const std::filesystem::path& save) override {
    auto rp=utf8(rom),sp=utf8(save);
    core_=mCoreFind(rp.c_str());if(!core_)return false;
    mCoreInitConfig(core_,"EmuFrame");
    if(!core_->init(core_)){core_=nullptr;return false;}
    core_->setVideoBuffer(core_,pixels_.data(),256);
    core_->setAudioBufferSize(core_,32768);
    if(!mCoreLoadFile(core_,rp.c_str()))return false;
    if(!mCoreLoadSaveFile(core_,sp.c_str(),false))return false;
    core_->reset(core_);
    mAudioBufferInit(&output_audio_,32768,core_->getAudioBuffer(core_)->channels);
    mAudioResamplerInit(&resampler_,mINTERPOLATOR_SINC);
    audio_ready_=true;
    mAudioResamplerSetSource(&resampler_,core_->getAudioBuffer(core_),core_->audioSampleRate(core_),true);
    mAudioResamplerSetDestination(&resampler_,&output_audio_,output_rate_);
    unsigned w=0,h=0;core_->currentVideoSize(core_,&w,&h);
    frame_={w,h,256*4,EF_PIXEL_RGBA8888,0};return w>0&&h>0&&w<=256&&h<=224;
  }
  void reset() override {core_->reset(core_);reset_audio();frame_.sequence++;}
  void run_frame() override {core_->runFrame(core_);mAudioResamplerProcess(&resampler_);unsigned w=0,h=0;core_->currentVideoSize(core_,&w,&h);frame_.width=w;frame_.height=h;frame_.sequence++;}
  void set_input(uint32_t bits) override {core_->setKeys(core_,bits&0x3ffu);}
  EF_VideoFrame video_info() const override {return frame_;}
  const uint32_t* video_pixels() const override {return pixels_.data();}
  EF_AudioInfo audio_info() const override {return {output_rate_,audio_ready_?output_audio_.channels:0u,audio_ready_?mAudioBufferAvailable(&output_audio_):0u};}
  size_t read_audio(int16_t* out,size_t frames) override {return audio_ready_?mAudioBufferRead(&output_audio_,out,frames):0;}
  bool save_state(const std::filesystem::path& path) override {auto p=utf8(path);auto* f=VFileOpen(p.c_str(),O_CREAT|O_TRUNC|O_RDWR);if(!f)return false;bool ok=mCoreSaveStateNamed(core_,f,SAVESTATE_RTC);f->close(f);return ok;}
  bool load_state(const std::filesystem::path& path) override {auto p=utf8(path);auto* f=VFileOpen(p.c_str(),O_RDONLY);if(!f)return false;bool ok=mCoreLoadStateNamed(core_,f,SAVESTATE_RTC);f->close(f);if(ok)reset_audio();return ok;}
  const char* name() const override {return "mGBA";}
};
}
std::unique_ptr<Backend> create_mgba_backend(){return std::make_unique<MGBABackend>();}
}
