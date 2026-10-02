#include <emuframe/emuframe.h>
#include "../apps/test_host/settings.hpp"
#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>
namespace {
int failures=0;
void check(bool ok,const char* name){if(!ok){std::fprintf(stderr,"FAIL: %s\n",name);++failures;}}
void count_game(const EF_GameInfo* info,void* user){if(info&&info->system==EF_SYSTEM_GBA)++*static_cast<int*>(user);}
}
int main(){
  check(ef_api_version()==EF_API_VERSION,"API version");
  check(ef_run_frame(999999)==EF_ERROR_INVALID_HANDLE,"invalid handle");
  EF_Handle a=0,b=0;EF_Config c{};c.struct_size=sizeof c;c.audio_enabled=1;c.volume=1;
  auto root=std::filesystem::temp_directory_path()/"EmuFrameTestData";
  std::error_code ec;std::filesystem::create_directories(root,ec);auto data=(root/"data").string();c.data_directory=data.c_str();
  std::string warning;auto settings_file=root/"emuframe.ini";auto settings=load_settings(settings_file,warning);
  check(std::filesystem::exists(settings_file)&&settings.audio_enabled&&settings.integer_scaling,"settings creation");
  {std::ofstream out(settings_file);out<<"audio_enabled=false\nvolume=0.5\ninteger_scaling=false\nmax_audio_backlog_ms=100\n";}
  warning.clear();settings=load_settings(settings_file,warning);
  check(!settings.audio_enabled&&settings.volume==0.5f&&!settings.integer_scaling&&settings.max_audio_backlog_ms==100,"settings parsing");
  check(ef_create_instance(EF_SYSTEM_AUTO,&c,&a)==EF_OK,"create auto");
  check(ef_create_instance(EF_SYSTEM_GBA,&c,&b)==EF_OK,"create GBA");
  check(a&&b&&a!=b,"distinct handles");
  EF_Status status=EF_STATUS_RUNNING;check(ef_get_status(a,&status)==EF_OK&&status==EF_STATUS_EMPTY,"initial status");
  check(ef_load_rom(a,"this_rom_does_not_exist.gb")==EF_ERROR_FILE_NOT_FOUND,"missing ROM");
  char error[100]{};check(ef_get_last_error(a,error,sizeof error)==EF_OK&&std::strlen(error),"error detail");
  check(ef_run_frame(a)==EF_ERROR_BAD_STATE,"frame without ROM");

  /* Tiny homebrew ARM loop: no game data, firmware, or Nintendo logo. */
  auto rompath=root/"loop.gba";std::array<unsigned char,512> rom{};
  rom[0]=0xFE;rom[1]=0xFF;rom[2]=0xFF;rom[3]=0xEA;rom[0xB2]=0x96;
  std::memcpy(rom.data()+0xA0,"EMUFRAME TEST",13);
  {std::ofstream out(rompath,std::ios::binary|std::ios::trunc);out.write((char*)rom.data(),rom.size());}
  auto path=rompath.string();auto load=ef_load_rom(a,path.c_str());
  if(load!=EF_OK){char detail[256]{};ef_get_last_error(a,detail,sizeof detail);std::fprintf(stderr,"ROM load: %s\n",detail);}
  check(load==EF_OK,"load synthetic GBA");
  if(load==EF_OK){
    EF_GameInfo game{};check(ef_get_game_info(a,&game)==EF_OK&&game.system==EF_SYSTEM_GBA&&std::strcmp(game.sha256,"2be471b6a74b267a0de2743859c347247911f6da9e7641ce8fdda67b5249d5f5")==0,"game metadata and SHA-256");
    int scanned=0;size_t found=0;auto root_path=root.string();
    check(ef_scan_library(root_path.c_str(),count_game,&scanned,&found)==EF_OK&&scanned==1&&found==1,"library scan");
    auto dbpath=(root/"library.db").string();check(ef_index_library(root_path.c_str(),dbpath.c_str(),&found)==EF_OK&&found==1,"library index");
    check(ef_record_library_play(dbpath.c_str(),game.sha256,30)==EF_OK,"library play history");
    check(ef_start(a)==EF_OK,"start");
    EF_InputState input{};input.buttons=EF_BUTTON_A|EF_BUTTON_START;check(ef_set_input(a,&input)==EF_OK,"input");
    check(ef_run_frame(a)==EF_OK,"run frame");
    EF_VideoFrame frame{};check(ef_get_video_info(a,&frame)==EF_OK&&frame.width==240&&frame.height==160,"video info");
    size_t bytes=0;check(ef_copy_video(a,nullptr,0,&bytes)==EF_ERROR_BUFFER_TOO_SMALL&&bytes>=frame.pitch*frame.height,"video size");
    std::vector<unsigned char> pixels(bytes);check(ef_copy_video(a,pixels.data(),pixels.size(),nullptr)==EF_OK,"video copy");
    EF_AudioInfo audio{};check(ef_get_audio_info(a,&audio)==EF_OK&&audio.channels>0&&audio.sample_rate>0,"audio info");
    std::vector<int16_t> pcm(2048*audio.channels);size_t audio_frames=0;
    check(ef_read_audio(a,pcm.data(),2048,&audio_frames)==EF_OK&&audio_frames>0,"audio read");
    check(ef_set_audio_options(a,1,0.5f)==EF_OK,"change audio options");
    check(ef_set_audio_options(a,1,3.0f)==EF_ERROR_INVALID_ARGUMENT,"reject invalid volume");
    check(ef_save_state(a,0)==EF_OK,"save state");check(ef_load_state(a,0)==EF_OK,"load state");
    check(ef_pause(a)==EF_OK,"pause");check(ef_run_frame(a)==EF_ERROR_BAD_STATE,"frame while paused");
    check(ef_resume(a)==EF_OK,"resume");check(ef_reset(a)==EF_OK,"reset");
    check(ef_close_rom(a)==EF_OK,"close ROM");
  }
  for(int color=0;color<2;color++){
    std::vector<unsigned char> gb(0x8000);
    gb[0x100]=0xC3;gb[0x101]=0x50;gb[0x102]=0x01; /* jump to a harmless loop */
    gb[0x104]=0xCE;gb[0x105]=0xED;gb[0x106]=0x66;gb[0x107]=0x66;
    std::memcpy(gb.data()+0x134,"EMUFRAME",8);gb[0x143]=color?0x80:0;
    gb[0x150]=0x18;gb[0x151]=0xFE;
    auto gbpath=root/(color?"loop.gbc":"loop.gb");
    {std::ofstream out(gbpath,std::ios::binary|std::ios::trunc);out.write((char*)gb.data(),gb.size());}
    auto gbname=gbpath.string();auto loaded=ef_load_rom(a,gbname.c_str());
    if(loaded!=EF_OK){char detail[256]{};ef_get_last_error(a,detail,sizeof detail);std::fprintf(stderr,"GB load: %s\n",detail);}
    check(loaded==EF_OK,color?"load synthetic GBC":"load synthetic GB");
    if(loaded==EF_OK){EF_GameInfo game{};ef_get_game_info(a,&game);check(game.system==(color?EF_SYSTEM_GBC:EF_SYSTEM_GB),"GB system detection");ef_start(a);check(ef_run_frame(a)==EF_OK,"GB run frame");EF_VideoFrame vf{};ef_get_video_info(a,&vf);check(vf.width==160&&vf.height==144,"GB video dimensions");ef_close_rom(a);}
  }
  ef_destroy_instance(a);check(ef_get_status(a,&status)==EF_ERROR_INVALID_HANDLE,"stale handle");
  ef_destroy_instance(b);std::filesystem::remove_all(root,ec);
  return failures?1:0;
}
