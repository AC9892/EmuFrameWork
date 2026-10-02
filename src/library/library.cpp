#include "../core/rom.hpp"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <string>
#include <vector>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif
extern "C" EF_Result ef_scan_library(const char* path,EF_LibraryCallback cb,void* user,size_t* found){
  if(!path||!cb||!found)return EF_ERROR_INVALID_ARGUMENT;*found=0;
  try{
    auto root=std::filesystem::u8path(path);
    if(!std::filesystem::is_directory(root))return EF_ERROR_FILE_NOT_FOUND;
    std::error_code ec;
    for(std::filesystem::recursive_directory_iterator it(root,std::filesystem::directory_options::skip_permission_denied,ec),end;it!=end;it.increment(ec)){
      if(ec){ec.clear();continue;}if(!it->is_regular_file(ec))continue;
      auto ext=it->path().extension().string();std::transform(ext.begin(),ext.end(),ext.begin(),[](unsigned char c){return char(std::tolower(c));});
      if(ext!=".gb"&&ext!=".gbc"&&ext!=".gba")continue;
      ef::Rom rom;std::string error;if(ef::inspect_rom(it->path(),rom,error)==EF_OK){cb(&rom.info,user);++*found;}
    }
    return EF_OK;
  }catch(...){return EF_ERROR_IO;}
}
namespace {
struct Entry {EF_GameInfo info{};uint64_t last_played=0,play_seconds=0;};
using Database=std::map<std::string,Entry>;
Database read_database(const std::filesystem::path& path){
  Database db;std::ifstream file(path);std::string magic;std::getline(file,magic);if(magic!="EmuFrameLibraryV1")return db;
  while(file){Entry e;std::string key,title,rompath,hash,save;int system=0;
    if(!(file>>std::quoted(key)>>system>>e.info.rom_size>>std::quoted(title)>>std::quoted(rompath)>>std::quoted(hash)>>e.last_played>>e.play_seconds))break;
    e.info.system=EF_System(system);std::snprintf(e.info.title,sizeof e.info.title,"%s",title.c_str());std::snprintf(e.info.loaded_path,sizeof e.info.loaded_path,"%s",rompath.c_str());std::snprintf(e.info.sha256,sizeof e.info.sha256,"%s",hash.c_str());db[key]=e;
  }return db;
}
bool write_database(const std::filesystem::path& path,const Database& db){
  auto temp=path;temp+=".tmp";{std::ofstream file(temp,std::ios::trunc);if(!file)return false;file<<"EmuFrameLibraryV1\n";
    for(auto& [key,e]:db)file<<std::quoted(key)<<' '<<int(e.info.system)<<' '<<e.info.rom_size<<' '<<std::quoted(e.info.title)<<' '<<std::quoted(e.info.loaded_path)<<' '<<std::quoted(e.info.sha256)<<' '<<e.last_played<<' '<<e.play_seconds<<'\n';
    file.flush();if(!file)return false;}
#ifdef _WIN32
  return MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
#else
  std::error_code ec;std::filesystem::rename(temp,path,ec);return !ec;
#endif
}
struct ScanContext {Database* db;size_t count=0;};
void collect(const EF_GameInfo* info,void* user){auto& ctx=*static_cast<ScanContext*>(user);auto key=std::string(info->loaded_path);auto& item=(*ctx.db)[key];item.info=*info;ctx.count++;}
}
extern "C" EF_Result ef_index_library(const char* dir,const char* database,size_t* found){
  if(!dir||!database||!found)return EF_ERROR_INVALID_ARGUMENT;
  try{auto path=std::filesystem::u8path(database);auto db=read_database(path);ScanContext ctx{&db};auto result=ef_scan_library(dir,collect,&ctx,found);if(result!=EF_OK)return result;std::error_code ec;auto parent=path.parent_path();if(!parent.empty())std::filesystem::create_directories(parent,ec);if(ec||!write_database(path,db))return EF_ERROR_IO;return EF_OK;}catch(...){return EF_ERROR_IO;}
}
extern "C" EF_Result ef_record_library_play(const char* database,const char* sha,uint64_t seconds){
  if(!database||!sha)return EF_ERROR_INVALID_ARGUMENT;
  try{auto path=std::filesystem::u8path(database);if(!std::filesystem::exists(path))return EF_ERROR_FILE_NOT_FOUND;auto db=read_database(path);bool changed=false;
    for(auto& [key,e]:db)if(std::string(e.info.sha256)==sha){e.last_played=uint64_t(std::time(nullptr));e.play_seconds+=seconds;changed=true;}
    if(!changed)return EF_ERROR_FILE_NOT_FOUND;return write_database(path,db)?EF_OK:EF_ERROR_IO;
  }catch(...){return EF_ERROR_IO;}
}
