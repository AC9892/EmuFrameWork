#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commdlg.h>
#include <mmsystem.h>
#include <Xinput.h>
#include <shellapi.h>
#include <emuframe/emuframe.h>
#include "settings.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

namespace {
enum { ID_OPEN=100,ID_CLOSE,ID_EXIT,ID_PAUSE,ID_RESUME,ID_RESET,ID_SAVE,ID_LOAD,ID_INFO,ID_BACKEND,ID_PERF,ID_LOGS,ID_OPEN_SETTINGS,ID_RELOAD_SETTINGS,ID_CONTROLLER };
HWND window_handle=nullptr;EF_Handle emulator=0;EF_Status status=EF_STATUS_EMPTY;
std::vector<uint8_t> rgba,bgra;EF_VideoFrame video{};std::string logs;
TestHostSettings settings;std::filesystem::path config_path,active_data_dir,log_path;
std::ofstream log_file;
std::string controller_source,last_controller_event;
HWAVEOUT audio_device=nullptr;WAVEFORMATEX audio_format{};
struct AudioBlock {WAVEHDR hdr{};std::vector<int16_t> samples;bool prepared=false;};
std::array<AudioBlock,4> audio_blocks{};
std::chrono::steady_clock::time_point previous_tick{};double frame_accum=0;
std::chrono::steady_clock::time_point last_audio_drop_log{};
std::string utf8(const wchar_t* text){int n=WideCharToMultiByte(CP_UTF8,0,text,-1,nullptr,0,nullptr,nullptr);std::string s(size_t(n),0);if(n)WideCharToMultiByte(CP_UTF8,0,text,-1,s.data(),n,nullptr,nullptr);if(!s.empty())s.pop_back();return s;}
std::string utf8(const std::filesystem::path& path){auto s=path.u8string();return std::string(s.begin(),s.end());}
std::filesystem::path data_dir_for(const TestHostSettings& s){auto p=std::filesystem::u8path(s.data_directory);return p.is_absolute()?p:config_path.parent_path()/p;}
void log_line(EF_LogLevel level,const char* message,void*){
  const char* names[]={"TRACE","DEBUG","INFO","WARN","ERROR","FATAL"};
  SYSTEMTIME now{};GetLocalTime(&now);char stamp[40]{};
  std::snprintf(stamp,sizeof stamp,"%04u-%02u-%02u %02u:%02u:%02u.%03u",now.wYear,now.wMonth,now.wDay,now.wHour,now.wMinute,now.wSecond,now.wMilliseconds);
  std::string line=std::string(stamp)+" "+names[level]+": "+message;
  logs+=line+"\r\n";if(logs.size()>6000)logs.erase(0,logs.size()-5000);
  if(log_file){log_file<<line<<'\n';log_file.flush();}
}
void error_box(EF_Result result){char msg[512]{};ef_get_last_error(emulator,msg,sizeof(msg));std::string text=msg[0]?msg:ef_result_string(result);MessageBoxA(window_handle,text.c_str(),"EmuFrame error",MB_OK|MB_ICONERROR);}
void close_audio(){if(!audio_device)return;waveOutReset(audio_device);for(auto& b:audio_blocks){if(b.prepared){waveOutUnprepareHeader(audio_device,&b.hdr,sizeof b.hdr);b.prepared=false;}}waveOutClose(audio_device);audio_device=nullptr;}
void open_audio(){close_audio();if(!settings.audio_enabled)return;EF_AudioInfo info{};if(ef_get_audio_info(emulator,&info)!=EF_OK||!info.channels||!info.sample_rate)return;
  audio_format={};audio_format.wFormatTag=WAVE_FORMAT_PCM;audio_format.nChannels=WORD(info.channels);audio_format.nSamplesPerSec=info.sample_rate;audio_format.wBitsPerSample=16;audio_format.nBlockAlign=WORD(info.channels*2);audio_format.nAvgBytesPerSec=info.sample_rate*audio_format.nBlockAlign;
  auto result=waveOutOpen(&audio_device,WAVE_MAPPER,&audio_format,0,0,CALLBACK_NULL);
  if(result!=MMSYSERR_NOERROR){audio_device=nullptr;char message[256]{};waveOutGetErrorTextA(result,message,sizeof message);log_line(EF_LOG_ERROR,(std::string("Audio device: ")+message).c_str(),nullptr);return;}
  size_t frames_per_block=std::max<size_t>(128,size_t(info.sample_rate)*settings.audio_latency_ms/(1000*audio_blocks.size()));
  for(auto& b:audio_blocks){b.samples.resize(frames_per_block*info.channels);b.hdr={};b.hdr.lpData=(LPSTR)b.samples.data();b.hdr.dwBufferLength=DWORD(b.samples.size()*2);b.prepared=false;}
  log_line(EF_LOG_INFO,("Audio opened: "+std::to_string(info.sample_rate)+" Hz, "+std::to_string(info.channels)+" channels").c_str(),nullptr);
}
void pump_audio(){
  EF_AudioInfo info{};if(ef_get_audio_info(emulator,&info)!=EF_OK||!info.channels)return;
  size_t keep=audio_device?size_t(info.sample_rate)*settings.max_audio_backlog_ms/1000:0;
  size_t dropped=0;static std::vector<int16_t> discard;discard.resize(4096*info.channels);
  while(info.available_frames>keep){size_t count=0;auto want=std::min<size_t>(4096,info.available_frames-keep);if(ef_read_audio(emulator,discard.data(),want,&count)!=EF_OK||!count)break;dropped+=count;if(ef_get_audio_info(emulator,&info)!=EF_OK)break;}
  if(dropped){auto now=std::chrono::steady_clock::now();if(!last_audio_drop_log.time_since_epoch().count()||now-last_audio_drop_log>std::chrono::seconds(5)){log_line(EF_LOG_WARN,("Discarded "+std::to_string(dropped)+" stale audio frames").c_str(),nullptr);last_audio_drop_log=now;}}
  if(!audio_device)return;
  for(auto& b:audio_blocks){if(b.prepared&&!(b.hdr.dwFlags&WHDR_DONE))continue;if(b.prepared){waveOutUnprepareHeader(audio_device,&b.hdr,sizeof b.hdr);b.prepared=false;}size_t count=0;auto capacity=b.samples.size()/audio_format.nChannels;EF_AudioInfo available{};if(ef_get_audio_info(emulator,&available)!=EF_OK||available.available_frames<capacity)break;if(ef_read_audio(emulator,b.samples.data(),capacity,&count)!=EF_OK||count!=capacity)break;b.hdr.dwBufferLength=DWORD(count*audio_format.nBlockAlign);b.hdr.dwFlags=0;auto result=waveOutPrepareHeader(audio_device,&b.hdr,sizeof b.hdr);if(result==MMSYSERR_NOERROR){b.prepared=true;result=waveOutWrite(audio_device,&b.hdr,sizeof b.hdr);if(result!=MMSYSERR_NOERROR){waveOutUnprepareHeader(audio_device,&b.hdr,sizeof b.hdr);b.prepared=false;}}if(result!=MMSYSERR_NOERROR){char message[256]{};waveOutGetErrorTextA(result,message,sizeof message);log_line(EF_LOG_ERROR,(std::string("Audio queue: ")+message).c_str(),nullptr);break;}}
}
void refresh_status(){char line[256];EF_Performance p{};ef_get_performance(emulator,&p);EF_GameInfo game{};ef_get_game_info(emulator,&game);const char* sys=game.system==EF_SYSTEM_GBA?"GBA":game.system==EF_SYSTEM_GBC?"GBC":game.system==EF_SYSTEM_GB?"GB":"No ROM";const char* state=status==EF_STATUS_RUNNING?"Running":status==EF_STATUS_PAUSED?"Paused":"Empty";std::snprintf(line,sizeof line,"EmuFrame Test Host | %s | %.1f FPS | %s",sys,p.emulated_fps,state);SetWindowTextA(window_handle,line);}
void close_rom(){close_audio();ef_close_rom(emulator);status=EF_STATUS_EMPTY;video={};rgba.clear();bgra.clear();InvalidateRect(window_handle,nullptr,TRUE);refresh_status();}
void open_rom(){wchar_t filename[MAX_PATH]{};OPENFILENAMEW dialog{};dialog.lStructSize=sizeof dialog;dialog.hwndOwner=window_handle;dialog.lpstrFilter=L"Game Boy ROMs (*.gb;*.gbc;*.gba)\0*.gb;*.gbc;*.gba\0All files (*.*)\0*.*\0";dialog.lpstrFile=filename;dialog.nMaxFile=MAX_PATH;dialog.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST;
  if(!GetOpenFileNameW(&dialog))return;close_rom();auto path=utf8(filename);auto r=ef_load_rom(emulator,path.c_str());if(r!=EF_OK){error_box(r);return;}r=ef_start(emulator);if(r!=EF_OK){error_box(r);close_rom();return;}status=EF_STATUS_RUNNING;open_audio();previous_tick={};frame_accum=0;refresh_status();}
void reload_settings(){std::string warning;auto updated=load_settings(config_path,warning);auto new_data=data_dir_for(updated);settings=updated;
  ef_set_audio_options(emulator,settings.audio_enabled?1:0,settings.volume);
  if(status==EF_STATUS_RUNNING)open_audio();else close_audio();
  SetWindowPos(window_handle,nullptr,0,0,settings.window_width,settings.window_height,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
  InvalidateRect(window_handle,nullptr,TRUE);
  if(new_data!=active_data_dir)warning+=" data_directory changes after restarting the test host.";
  if(!warning.empty())MessageBoxA(window_handle,warning.c_str(),"Settings",MB_OK|MB_ICONINFORMATION);
  log_line(EF_LOG_INFO,"Settings reloaded",nullptr);
}
void show_info(){EF_GameInfo g{};if(ef_get_game_info(emulator,&g)!=EF_OK)return;char msg[1500];std::snprintf(msg,sizeof msg,"Title: %s\nSystem: %s\nSize: %llu bytes\nSHA-256: %s\nSave: %s\nPath: %s",g.title,g.system==EF_SYSTEM_GBA?"Game Boy Advance":g.system==EF_SYSTEM_GBC?"Game Boy Color":"Game Boy",(unsigned long long)g.rom_size,g.sha256,g.save_type,g.loaded_path);MessageBoxA(window_handle,msg,"Game Info",MB_OK);}
void show_performance(){EF_Performance p{};ef_get_performance(emulator,&p);char msg[512];std::snprintf(msg,sizeof msg,"Emulated FPS: %.2f\nHost frame: %.2f ms\nEmulation frame: %.2f ms\nAudio queued: %llu frames\nFrames: %llu\nDropped: %llu",p.emulated_fps,p.host_frame_ms,p.emulation_frame_ms,(unsigned long long)p.audio_buffer_frames,(unsigned long long)p.frames,(unsigned long long)p.dropped_frames);MessageBoxA(window_handle,msg,"Performance",MB_OK);}
uint32_t keyboard_buttons(){uint32_t b=0;auto down=[](int key){return (GetAsyncKeyState(key)&0x8000)!=0;};if(down(VK_UP))b|=EF_BUTTON_UP;if(down(VK_DOWN))b|=EF_BUTTON_DOWN;if(down(VK_LEFT))b|=EF_BUTTON_LEFT;if(down(VK_RIGHT))b|=EF_BUTTON_RIGHT;if(down('Z'))b|=EF_BUTTON_A;if(down('X'))b|=EF_BUTTON_B;if(down(VK_RETURN))b|=EF_BUTTON_START;if(down(VK_BACK))b|=EF_BUTTON_SELECT;if(down('A'))b|=EF_BUTTON_L;if(down('S'))b|=EF_BUTTON_R;return b;}
void set_controller_source(const std::string& source){
  if(source==controller_source)return;
  controller_source=source;
  log_line(EF_LOG_INFO,(source.empty()?"Controller disconnected":"Controller detected: "+source).c_str(),nullptr);
}
uint32_t controller_buttons(){
  XINPUT_STATE state{};uint32_t b=0;DWORD xinput_id=0;
  for(;xinput_id<XUSER_MAX_COUNT;++xinput_id)if(XInputGetState(xinput_id,&state)==ERROR_SUCCESS)break;
  if(xinput_id<XUSER_MAX_COUNT){
    set_controller_source("XInput controller "+std::to_string(xinput_id+1));
    const auto& pad=state.Gamepad;
    if(pad.wButtons)last_controller_event="XInput button mask: "+std::to_string(pad.wButtons);
    if((pad.wButtons&XINPUT_GAMEPAD_DPAD_UP)||pad.sThumbLY>XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE)b|=EF_BUTTON_UP;
    if((pad.wButtons&XINPUT_GAMEPAD_DPAD_DOWN)||pad.sThumbLY<-XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE)b|=EF_BUTTON_DOWN;
    if((pad.wButtons&XINPUT_GAMEPAD_DPAD_LEFT)||pad.sThumbLX<-XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE)b|=EF_BUTTON_LEFT;
    if((pad.wButtons&XINPUT_GAMEPAD_DPAD_RIGHT)||pad.sThumbLX>XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE)b|=EF_BUTTON_RIGHT;
    if(pad.wButtons&XINPUT_GAMEPAD_A)b|=EF_BUTTON_A;
    if(pad.wButtons&XINPUT_GAMEPAD_B)b|=EF_BUTTON_B;
    if(pad.wButtons&XINPUT_GAMEPAD_START)b|=EF_BUTTON_START;
    if(pad.wButtons&XINPUT_GAMEPAD_BACK)b|=EF_BUTTON_SELECT;
    if(pad.wButtons&XINPUT_GAMEPAD_LEFT_SHOULDER)b|=EF_BUTTON_L;
    if(pad.wButtons&XINPUT_GAMEPAD_RIGHT_SHOULDER)b|=EF_BUTTON_R;
    return b;
  }
  const UINT count=std::min<UINT>(joyGetNumDevs(),16);
  for(UINT id=0;id<count;++id){
    JOYINFOEX joy{};joy.dwSize=sizeof joy;joy.dwFlags=JOY_RETURNALL;
    if(joyGetPosEx(id,&joy)!=JOYERR_NOERROR)continue;
    JOYCAPSW caps{};if(joyGetDevCapsW(id,&caps,sizeof caps)!=JOYERR_NOERROR)continue;
    set_controller_source(utf8(caps.szPname)+" (Windows joystick "+std::to_string(id)+")");
    const auto pov=joy.dwPOV;
    if(pov!=JOY_POVCENTERED){
      if(pov<=4500||pov>=31500)b|=EF_BUTTON_UP;
      if(pov>=4500&&pov<=13500)b|=EF_BUTTON_RIGHT;
      if(pov>=13500&&pov<=22500)b|=EF_BUTTON_DOWN;
      if(pov>=22500&&pov<=31500)b|=EF_BUTTON_LEFT;
    }
    const auto xmid=(uint32_t(caps.wXmin)+caps.wXmax)/2,ymid=(uint32_t(caps.wYmin)+caps.wYmax)/2;
    const auto xdead=(uint32_t(caps.wXmax)-caps.wXmin)/4,ydead=(uint32_t(caps.wYmax)-caps.wYmin)/4;
    if(joy.dwXpos+xdead<xmid)b|=EF_BUTTON_LEFT;
    if(joy.dwXpos>xmid+xdead)b|=EF_BUTTON_RIGHT;
    if(joy.dwYpos+ydead<ymid)b|=EF_BUTTON_UP;
    if(joy.dwYpos>ymid+ydead)b|=EF_BUTTON_DOWN;
    const auto buttons=joy.dwButtons;
    if(buttons||pov!=JOY_POVCENTERED)last_controller_event="Joystick buttons: "+std::to_string(buttons)+", POV: "+std::to_string(pov);
    if(buttons&(1u<<1))b|=EF_BUTTON_A;      // DualSense Cross
    if(buttons&(1u<<2))b|=EF_BUTTON_B;      // DualSense Circle
    if(buttons&(1u<<8))b|=EF_BUTTON_SELECT; // Create
    if(buttons&(1u<<9))b|=EF_BUTTON_START;  // Options
    if(buttons&(1u<<4))b|=EF_BUTTON_L;
    if(buttons&(1u<<5))b|=EF_BUTTON_R;
    return b;
  }
  if(!controller_source.empty())set_controller_source({});
  return 0;
}
void tick(){if(status!=EF_STATUS_RUNNING)return;auto now=std::chrono::steady_clock::now();if(previous_tick.time_since_epoch().count())frame_accum+=std::chrono::duration<double>(now-previous_tick).count();previous_tick=now;frame_accum=std::min(frame_accum,0.1);int frames=0;
  pump_audio();
  while((settings.frame_limit?frame_accum>=1.0/59.7275:frames==0)&&frames<3){EF_InputState input{};if(GetForegroundWindow()==window_handle)input.buttons=keyboard_buttons()|controller_buttons();ef_set_input(emulator,&input);if(ef_run_frame(emulator)!=EF_OK){status=EF_STATUS_PAUSED;break;}if(settings.frame_limit)frame_accum-=1.0/59.7275;frames++;}
  if(frames){EF_VideoFrame next{};size_t required=0;if(ef_get_video_info(emulator,&next)==EF_OK&&ef_copy_video(emulator,nullptr,0,&required)==EF_ERROR_BUFFER_TOO_SMALL&&required){rgba.resize(required);if(ef_copy_video(emulator,rgba.data(),rgba.size(),nullptr)==EF_OK){video=next;bgra.resize(required);for(size_t i=0;i+3<required;i+=4){bgra[i]=rgba[i+2];bgra[i+1]=rgba[i+1];bgra[i+2]=rgba[i];bgra[i+3]=0;}InvalidateRect(window_handle,nullptr,FALSE);}else log_line(EF_LOG_ERROR,"Video copy failed; keeping previous frame",nullptr);}refresh_status();}
  pump_audio();
}
HMENU make_menu(){auto root=CreateMenu(),file=CreatePopupMenu(),emu=CreatePopupMenu(),debug=CreatePopupMenu(),options=CreatePopupMenu();AppendMenuW(file,MF_STRING,ID_OPEN,L"Open ROM...");AppendMenuW(file,MF_STRING,ID_CLOSE,L"Close ROM");AppendMenuW(file,MF_SEPARATOR,0,nullptr);AppendMenuW(file,MF_STRING,ID_EXIT,L"Exit");AppendMenuW(emu,MF_STRING,ID_PAUSE,L"Pause");AppendMenuW(emu,MF_STRING,ID_RESUME,L"Resume");AppendMenuW(emu,MF_STRING,ID_RESET,L"Reset");AppendMenuW(emu,MF_SEPARATOR,0,nullptr);AppendMenuW(emu,MF_STRING,ID_SAVE,L"Save State (slot 0)");AppendMenuW(emu,MF_STRING,ID_LOAD,L"Load State (slot 0)");AppendMenuW(options,MF_STRING,ID_OPEN_SETTINGS,L"Open Settings File");AppendMenuW(options,MF_STRING,ID_RELOAD_SETTINGS,L"Reload Settings");AppendMenuW(debug,MF_STRING,ID_INFO,L"Game Info");AppendMenuW(debug,MF_STRING,ID_BACKEND,L"Backend Info");AppendMenuW(debug,MF_STRING,ID_PERF,L"Performance");AppendMenuW(debug,MF_STRING,ID_CONTROLLER,L"Controller Info");AppendMenuW(debug,MF_STRING,ID_LOGS,L"Open Log File");AppendMenuW(root,MF_POPUP,(UINT_PTR)file,L"File");AppendMenuW(root,MF_POPUP,(UINT_PTR)emu,L"Emulation");AppendMenuW(root,MF_POPUP,(UINT_PTR)options,L"Settings");AppendMenuW(root,MF_POPUP,(UINT_PTR)debug,L"Debug");return root;}
LRESULT CALLBACK wndproc(HWND hwnd,UINT msg,WPARAM w,LPARAM l){switch(msg){
case WM_CREATE:window_handle=hwnd;SetMenu(hwnd,make_menu());SetTimer(hwnd,1,8,nullptr);refresh_status();return 0;
case WM_ACTIVATEAPP:if(!w){EF_InputState released{};ef_set_input(emulator,&released);}return 0;
case WM_TIMER:tick();return 0;
case WM_COMMAND:{EF_Result r=EF_OK;switch(LOWORD(w)){case ID_OPEN:open_rom();break;case ID_CLOSE:close_rom();break;case ID_EXIT:DestroyWindow(hwnd);break;case ID_PAUSE:r=ef_pause(emulator);if(r==EF_OK){status=EF_STATUS_PAUSED;close_audio();}break;case ID_RESUME:r=ef_resume(emulator);if(r==EF_OK){status=EF_STATUS_RUNNING;previous_tick={};open_audio();}break;case ID_RESET:r=ef_reset(emulator);break;case ID_SAVE:r=ef_save_state(emulator,0);break;case ID_LOAD:r=ef_load_state(emulator,0);break;case ID_OPEN_SETTINGS:if((INT_PTR)ShellExecuteW(hwnd,L"open",config_path.c_str(),nullptr,nullptr,SW_SHOWNORMAL)<=32)MessageBoxW(hwnd,L"Could not open emuframe.ini",L"Settings",MB_OK|MB_ICONERROR);break;case ID_RELOAD_SETTINGS:reload_settings();break;case ID_INFO:show_info();break;case ID_BACKEND:MessageBoxA(hwnd,ef_backend_name(),"Backend Info",MB_OK);break;case ID_PERF:show_performance();break;case ID_CONTROLLER:{controller_buttons();std::string info=controller_source.empty()?"No controller detected":controller_source;if(!last_controller_event.empty())info+="\nLast input: "+last_controller_event;MessageBoxA(hwnd,info.c_str(),"Controller Info",MB_OK);break;}case ID_LOGS:if(!log_file||(INT_PTR)ShellExecuteW(hwnd,L"open",log_path.c_str(),nullptr,nullptr,SW_SHOWNORMAL)<=32)MessageBoxA(hwnd,logs.c_str(),"Recent Logs",MB_OK);break;}if(r!=EF_OK)error_box(r);refresh_status();return 0;}
case WM_PAINT:{PAINTSTRUCT ps;auto dc=BeginPaint(hwnd,&ps);RECT rc;GetClientRect(hwnd,&rc);HBRUSH brush=CreateSolidBrush(RGB(25,25,28));FillRect(dc,&rc,brush);DeleteObject(brush);if(video.width&&video.height&&!bgra.empty()){int aw=rc.right-rc.left,ah=rc.bottom-rc.top;int dw=aw,dh=ah;if(settings.preserve_aspect_ratio){double s=std::min(double(aw)/video.width,double(ah)/video.height);if(settings.integer_scaling&&s>=1)s=std::floor(s);dw=std::max(1,int(video.width*s));dh=std::max(1,int(video.height*s));}BITMAPINFO bmi{};bmi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);bmi.bmiHeader.biWidth=int(video.pitch/4);bmi.bmiHeader.biHeight=-int(video.height);bmi.bmiHeader.biPlanes=1;bmi.bmiHeader.biBitCount=32;bmi.bmiHeader.biCompression=BI_RGB;SetStretchBltMode(dc,COLORONCOLOR);StretchDIBits(dc,(aw-dw)/2,(ah-dh)/2,dw,dh,0,0,video.width,video.height,bgra.data(),&bmi,DIB_RGB_COLORS,SRCCOPY);}else{SetBkMode(dc,TRANSPARENT);SetTextColor(dc,RGB(210,210,210));DrawTextW(dc,L"File > Open ROM to start",-1,&rc,DT_CENTER|DT_VCENTER|DT_SINGLELINE);}EndPaint(hwnd,&ps);return 0;}
case WM_DESTROY:KillTimer(hwnd,1);close_audio();log_line(EF_LOG_INFO,"Test host closing",nullptr);ef_destroy_instance(emulator);if(log_file)log_file.flush();PostQuitMessage(0);return 0;
}return DefWindowProcW(hwnd,msg,w,l);}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int show){
  config_path=settings_path();std::string warning;settings=load_settings(config_path,warning);
  active_data_dir=data_dir_for(settings);log_path=active_data_dir/"logs"/"test-host.log";
  std::error_code ec;std::filesystem::create_directories(log_path.parent_path(),ec);
  if(!ec){log_file.open(log_path,std::ios::app);if(log_file)log_file<<"\n=== EmuFrame Test Host session ===\n";}
  if(!log_file)warning+=" Could not open persistent log file.";
  auto data_utf8=utf8(active_data_dir);EF_Config config{};config.struct_size=sizeof config;config.audio_enabled=settings.audio_enabled?1:0;config.frame_limit=settings.frame_limit?1:0;config.integer_scaling=settings.integer_scaling?1:0;config.volume=settings.volume;config.data_directory=data_utf8.c_str();
  EF_Result result=ef_create_instance(EF_SYSTEM_AUTO,&config,&emulator);if(result!=EF_OK)return 1;
  ef_set_log_callback(emulator,log_line,nullptr);log_line(EF_LOG_INFO,"Test host started",nullptr);if(!warning.empty())log_line(EF_LOG_WARN,warning.c_str(),nullptr);
  WNDCLASSW cls{};cls.lpfnWndProc=wndproc;cls.hInstance=instance;cls.lpszClassName=L"EmuFrameTestHost";cls.hCursor=LoadCursor(nullptr,IDC_ARROW);RegisterClassW(&cls);
  auto hwnd=CreateWindowExW(0,cls.lpszClassName,L"EmuFrame Test Host",WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,settings.window_width,settings.window_height,nullptr,nullptr,instance,nullptr);ShowWindow(hwnd,show);
  MSG msg;while(GetMessageW(&msg,nullptr,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);}return int(msg.wParam);
}
