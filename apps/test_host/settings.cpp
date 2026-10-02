#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "settings.hpp"
#include <algorithm>
#include <charconv>
#include <fstream>
#include <sstream>

namespace {
std::string trim(std::string s){auto first=s.find_first_not_of(" \t\r\n");if(first==std::string::npos)return {};auto last=s.find_last_not_of(" \t\r\n");return s.substr(first,last-first+1);}
bool parse_bool(const std::string& s,bool& out){if(s=="true"||s=="1"||s=="yes"){out=true;return true;}if(s=="false"||s=="0"||s=="no"){out=false;return true;}return false;}
bool parse_int(const std::string& s,int& out){auto [p,e]=std::from_chars(s.data(),s.data()+s.size(),out);return e==std::errc{}&&p==s.data()+s.size();}
std::string defaults(){return R"(# EmuFrame Test Host settings. Edit this file, then use File > Reload Settings.
# Paths are relative to this file's folder. ROM files are never changed.
audio_enabled=true
volume=1.0
frame_limit=true
integer_scaling=true
preserve_aspect_ratio=true
audio_latency_ms=80
max_audio_backlog_ms=120
window_width=760
window_height=560
data_directory=EmuFrameData
)";}
}
std::filesystem::path settings_path(){wchar_t path[32768]{};auto n=GetModuleFileNameW(nullptr,path,32768);return std::filesystem::path(std::wstring(path,n)).parent_path()/"emuframe.ini";}
TestHostSettings load_settings(const std::filesystem::path& path,std::string& warning){
  TestHostSettings result;
  if(!std::filesystem::exists(path)){std::ofstream created(path);if(created)created<<defaults();else warning="Could not create emuframe.ini; using defaults.";}
  std::ifstream file(path);if(!file){warning="Could not read emuframe.ini; using defaults.";return result;}
  std::string line;int number=0;
  while(std::getline(file,line)){++number;line=trim(line);if(line.empty()||line[0]=='#'||line[0]==';')continue;auto equals=line.find('=');if(equals==std::string::npos){warning+="Invalid settings line "+std::to_string(number)+". ";continue;}
    auto key=trim(line.substr(0,equals)),value=trim(line.substr(equals+1));bool valid=true;
    if(key=="audio_enabled")valid=parse_bool(value,result.audio_enabled);
    else if(key=="frame_limit")valid=parse_bool(value,result.frame_limit);
    else if(key=="integer_scaling")valid=parse_bool(value,result.integer_scaling);
    else if(key=="preserve_aspect_ratio")valid=parse_bool(value,result.preserve_aspect_ratio);
    else if(key=="volume"){try{size_t used=0;auto v=std::stof(value,&used);valid=used==value.size()&&v>=0&&v<=2;if(valid)result.volume=v;}catch(...){valid=false;}}
    else if(key=="audio_latency_ms"){int v=0;valid=parse_int(value,v)&&v>=40&&v<=250;if(valid)result.audio_latency_ms=v;}
    else if(key=="max_audio_backlog_ms"){int v=0;valid=parse_int(value,v)&&v>=40&&v<=500;if(valid)result.max_audio_backlog_ms=v;}
    else if(key=="window_width"){int v=0;valid=parse_int(value,v)&&v>=320&&v<=3840;if(valid)result.window_width=v;}
    else if(key=="window_height"){int v=0;valid=parse_int(value,v)&&v>=240&&v<=2160;if(valid)result.window_height=v;}
    else if(key=="data_directory"){valid=!value.empty();if(valid)result.data_directory=value;}
    else {warning+="Unknown setting '"+key+"'. ";continue;}
    if(!valid)warning+="Invalid value for '"+key+"' on line "+std::to_string(number)+". ";
  }
  return result;
}
