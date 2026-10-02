#include "instance.hpp"
#include <fstream>
#include <iomanip>
#include <sstream>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif
namespace ef {
namespace {
std::string json_escape(const std::string& x){std::string r;for(char c:x){if(c=='"'||c=='\\')r+='\\';if(c>=32)r+=c;}return r;}
bool replace_file(const std::filesystem::path& from,const std::filesystem::path& to){
#ifdef _WIN32
  return MoveFileExW(from.c_str(),to.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
#else
  std::error_code ec;std::filesystem::rename(from,to,ec);return !ec;
#endif
}
}
Instance::Instance(EF_System p,const EF_Config* c):preferred(p){
  if(c){audio_enabled=c->audio_enabled!=0;volume=c->volume; if(c->data_directory&&*c->data_directory)data_dir=std::filesystem::u8path(c->data_directory);}
  if(data_dir.empty())data_dir="EmuFrameData";
}
void Instance::log(EF_LogLevel level,const std::string& message){if(logger)logger(level,message.c_str(),logger_user);}
EF_Result Instance::fail(EF_Result code,const std::string& message){last_error=message;log(EF_LOG_ERROR,message);return code;}
EF_Result Instance::load(const char* path){
  if(!path||!*path)return fail(EF_ERROR_INVALID_ARGUMENT,"ROM path is empty");
  Rom next;std::string error;auto rompath=std::filesystem::u8path(path);auto result=inspect_rom(rompath,next,error);
  if(result!=EF_OK)return fail(result,error);
  if(preferred!=EF_SYSTEM_AUTO&&preferred!=next.info.system)return fail(EF_ERROR_UNSUPPORTED_SYSTEM,"ROM system differs from requested system");
  auto save_dir=data_dir/"saves"/next.info.sha256;
  std::error_code ec;std::filesystem::create_directories(save_dir,ec);if(ec)return fail(EF_ERROR_IO,"Cannot create save directory: "+ec.message());
  auto candidate=create_mgba_backend();
  if(!candidate->load(next.path,save_dir/"game.sav"))return fail(EF_ERROR_BACKEND_FAILURE,"mGBA could not load the ROM or cartridge save");
  backend=std::move(candidate);rom=std::move(next);status=EF_STATUS_PAUSED;performance={};last_frame={};fps_window_start={};fps_window_frames=0;last_error.clear();
  auto meta=save_dir/"metadata.json",tmp=save_dir/"metadata.json.tmp";
  {std::ofstream f(tmp,std::ios::trunc);if(f)f<<"{\"title\":\""<<json_escape(rom.info.title)<<"\",\"sha256\":\""<<rom.info.sha256<<"\",\"system\":"<<int(rom.info.system)<<"}\n";}
  replace_file(tmp,meta);
  log(EF_LOG_INFO,std::string("Loaded ")+rom.info.title+" ("+rom.info.sha256+") using mGBA");return EF_OK;
}
EF_Result Instance::close(){backend.reset();rom={};status=EF_STATUS_EMPTY;performance={};fps_window_start={};fps_window_frames=0;return EF_OK;}
EF_Result Instance::start(){if(!backend)return fail(EF_ERROR_BAD_STATE,"No ROM loaded");status=EF_STATUS_RUNNING;last_frame={};fps_window_start={};fps_window_frames=0;return EF_OK;}
EF_Result Instance::pause(){if(!backend)return fail(EF_ERROR_BAD_STATE,"No ROM loaded");status=EF_STATUS_PAUSED;return EF_OK;}
EF_Result Instance::resume(){return start();}
EF_Result Instance::reset(){if(!backend)return fail(EF_ERROR_BAD_STATE,"No ROM loaded");backend->reset();return EF_OK;}
EF_Result Instance::stop(){return close();}
EF_Result Instance::frame(){
  if(!backend||status!=EF_STATUS_RUNNING)return fail(EF_ERROR_BAD_STATE,"Emulation is not running");
  auto begin=std::chrono::steady_clock::now();backend->run_frame();auto end=std::chrono::steady_clock::now();
  performance.emulation_frame_ms=std::chrono::duration<double,std::milli>(end-begin).count();
  if(last_frame.time_since_epoch().count()){auto dt=std::chrono::duration<double,std::milli>(end-last_frame).count();performance.host_frame_ms=performance.host_frame_ms?performance.host_frame_ms*0.9+dt*0.1:dt;}
  last_frame=end;performance.frames++;if(!fps_window_start.time_since_epoch().count())fps_window_start=end;
  ++fps_window_frames;auto elapsed=std::chrono::duration<double>(end-fps_window_start).count();
  if(elapsed>=0.5){performance.emulated_fps=fps_window_frames/elapsed;fps_window_start=end;fps_window_frames=0;}
  performance.audio_buffer_frames=backend->audio_info().available_frames;return EF_OK;
}
EF_Result Instance::input(const EF_InputState* value){if(!value)return fail(EF_ERROR_INVALID_ARGUMENT,"Input pointer is null");if(!backend)return fail(EF_ERROR_BAD_STATE,"No ROM loaded");backend->set_input(value->buttons);return EF_OK;}
EF_Result Instance::state(uint32_t slot,bool save){
  if(!backend)return fail(EF_ERROR_BAD_STATE,"No ROM loaded");if(slot>99)return fail(EF_ERROR_INVALID_ARGUMENT,"State slot must be 0 through 99");
  auto dir=data_dir/"states"/rom.info.sha256;std::error_code ec;std::filesystem::create_directories(dir,ec);if(ec)return fail(EF_ERROR_SAVE_FAILED,"Cannot create state directory");
  auto path=dir/("slot_"+std::to_string(slot)+".state");
  if(save){auto tmp=dir/("slot_"+std::to_string(slot)+".state.tmp");if(!backend->save_state(tmp)||!replace_file(tmp,path)){std::filesystem::remove(tmp,ec);return fail(EF_ERROR_SAVE_FAILED,"Could not write save state");}log(EF_LOG_INFO,"Save state created");return EF_OK;}
  if(!std::filesystem::exists(path))return fail(EF_ERROR_FILE_NOT_FOUND,"Save state does not exist");
  if(!backend->load_state(path))return fail(EF_ERROR_SAVE_FAILED,"Could not load save state");return EF_OK;
}
}
