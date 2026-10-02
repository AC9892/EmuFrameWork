#include "../core/instance.hpp"
#include <algorithm>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
namespace {
std::mutex registry_mutex;
std::map<EF_Handle,std::shared_ptr<ef::Instance>> registry;
EF_Handle next_handle=1;
std::shared_ptr<ef::Instance> find(EF_Handle h){std::lock_guard lock(registry_mutex);auto it=registry.find(h);return it==registry.end()?nullptr:it->second;}
template<class F> EF_Result call(EF_Handle h,F&& f){auto x=find(h);if(!x)return EF_ERROR_INVALID_HANDLE;try{std::lock_guard lock(x->mutex);return f(*x);}catch(const std::exception& e){std::lock_guard lock(x->mutex);return x->fail(EF_ERROR_BACKEND_FAILURE,e.what());}catch(...){return EF_ERROR_BACKEND_FAILURE;}}
}
extern "C" {
uint32_t ef_api_version(void){return EF_API_VERSION;}
EF_Result ef_create_instance(EF_System p,const EF_Config* c,EF_Handle* out){
  if(!out||p<EF_SYSTEM_AUTO||p>EF_SYSTEM_GBA|| (c&&c->struct_size<sizeof(EF_Config)))return EF_ERROR_INVALID_ARGUMENT;
  try{auto x=std::make_shared<ef::Instance>(p,c);std::lock_guard lock(registry_mutex);*out=next_handle++;registry[*out]=std::move(x);return EF_OK;}catch(...){return EF_ERROR_BACKEND_FAILURE;}
}
void ef_destroy_instance(EF_Handle h){std::lock_guard lock(registry_mutex);registry.erase(h);}
EF_Result ef_load_rom(EF_Handle h,const char* p){return call(h,[&](auto& x){return x.load(p);});}
EF_Result ef_close_rom(EF_Handle h){return call(h,[](auto& x){return x.close();});}
EF_Result ef_start(EF_Handle h){return call(h,[](auto& x){return x.start();});}
EF_Result ef_pause(EF_Handle h){return call(h,[](auto& x){return x.pause();});}
EF_Result ef_resume(EF_Handle h){return call(h,[](auto& x){return x.resume();});}
EF_Result ef_reset(EF_Handle h){return call(h,[](auto& x){return x.reset();});}
EF_Result ef_stop(EF_Handle h){return call(h,[](auto& x){return x.stop();});}
EF_Result ef_run_frame(EF_Handle h){return call(h,[](auto& x){return x.frame();});}
EF_Result ef_set_input(EF_Handle h,const EF_InputState* v){return call(h,[&](auto& x){return x.input(v);});}
EF_Result ef_get_status(EF_Handle h,EF_Status* out){if(!out)return EF_ERROR_INVALID_ARGUMENT;return call(h,[&](auto& x){*out=x.status;return EF_OK;});}
EF_Result ef_get_game_info(EF_Handle h,EF_GameInfo* out){if(!out)return EF_ERROR_INVALID_ARGUMENT;return call(h,[&](auto& x){if(!x.backend)return EF_ERROR_BAD_STATE;*out=x.rom.info;return EF_OK;});}
EF_Result ef_get_video_info(EF_Handle h,EF_VideoFrame* out){if(!out)return EF_ERROR_INVALID_ARGUMENT;return call(h,[&](auto& x){if(!x.backend)return EF_ERROR_BAD_STATE;*out=x.backend->video_info();return EF_OK;});}
EF_Result ef_copy_video(EF_Handle h,void* dst,size_t bytes,size_t* required){return call(h,[&](auto& x){if(!x.backend)return EF_ERROR_BAD_STATE;auto info=x.backend->video_info();size_t needed=size_t(info.pitch)*info.height;if(required)*required=needed;if(!dst||bytes<needed)return EF_ERROR_BUFFER_TOO_SMALL;std::memcpy(dst,x.backend->video_pixels(),needed);return EF_OK;});}
EF_Result ef_get_audio_info(EF_Handle h,EF_AudioInfo* out){if(!out)return EF_ERROR_INVALID_ARGUMENT;return call(h,[&](auto& x){if(!x.backend)return EF_ERROR_BAD_STATE;*out=x.backend->audio_info();return EF_OK;});}
EF_Result ef_set_audio_options(EF_Handle h,uint8_t enabled,float volume){if(!(volume>=0.0f&&volume<=2.0f))return EF_ERROR_INVALID_ARGUMENT;return call(h,[&](auto& x){x.audio_enabled=enabled!=0;x.volume=volume;return EF_OK;});}
EF_Result ef_read_audio(EF_Handle h,int16_t* dst,size_t capacity,size_t* written){if(!written||(!dst&&capacity))return EF_ERROR_INVALID_ARGUMENT;return call(h,[&](auto& x){if(!x.backend)return EF_ERROR_BAD_STATE;*written=x.backend->read_audio(dst,capacity);if(!x.audio_enabled){std::memset(dst,0,*written*x.backend->audio_info().channels*sizeof(int16_t));}else if(x.volume!=1.0f){auto channels=x.backend->audio_info().channels;for(size_t i=0;i<*written*channels;i++)dst[i]=(int16_t)std::clamp(int(dst[i]*x.volume),-32768,32767);}return EF_OK;});}
EF_Result ef_save_state(EF_Handle h,uint32_t s){return call(h,[&](auto& x){return x.state(s,true);});}
EF_Result ef_load_state(EF_Handle h,uint32_t s){return call(h,[&](auto& x){return x.state(s,false);});}
EF_Result ef_get_performance(EF_Handle h,EF_Performance* out){if(!out)return EF_ERROR_INVALID_ARGUMENT;return call(h,[&](auto& x){*out=x.performance;if(x.backend)out->audio_buffer_frames=x.backend->audio_info().available_frames;return EF_OK;});}
EF_Result ef_set_log_callback(EF_Handle h,EF_LogCallback cb,void* user){return call(h,[&](auto& x){x.logger=cb;x.logger_user=user;return EF_OK;});}
EF_Result ef_get_last_error(EF_Handle h,char* out,size_t cap){if(!out||!cap)return EF_ERROR_INVALID_ARGUMENT;return call(h,[&](auto& x){std::snprintf(out,cap,"%s",x.last_error.c_str());return EF_OK;});}
const char* ef_result_string(EF_Result r){switch(r){case EF_OK:return "OK";case EF_ERROR_INVALID_HANDLE:return "Invalid handle";case EF_ERROR_INVALID_ARGUMENT:return "Invalid argument";case EF_ERROR_FILE_NOT_FOUND:return "File not found";case EF_ERROR_INVALID_ROM:return "Invalid ROM";case EF_ERROR_UNSUPPORTED_SYSTEM:return "Unsupported system";case EF_ERROR_BACKEND_FAILURE:return "Backend failure";case EF_ERROR_SAVE_FAILED:return "Save failed";case EF_ERROR_BAD_STATE:return "Invalid state";case EF_ERROR_BUFFER_TOO_SMALL:return "Buffer too small";case EF_ERROR_IO:return "I/O error";}return "Unknown error";}
const char* ef_backend_name(void){return "mGBA";}
}
