#include "rom.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <cstdio>
#include <fstream>
namespace ef {
namespace {
void copy(char* dest,size_t cap,const std::string& s){std::snprintf(dest,cap,"%s",s.c_str());}
std::string clean_title(const unsigned char* p,size_t n){std::string s;for(size_t i=0;i<n&&p[i];i++){unsigned char c=p[i];s+=c>=32&&c<127?char(c):'_';}while(!s.empty()&&s.back()==' ')s.pop_back();return s;}
}
EF_Result inspect_rom(const std::filesystem::path& path, Rom& out, std::string& error) {
  std::error_code ec; if(!std::filesystem::is_regular_file(path,ec)){error="ROM file not found";return EF_ERROR_FILE_NOT_FOUND;}
  auto size=std::filesystem::file_size(path,ec);if(ec||size<0x150){error="ROM is too small";return EF_ERROR_INVALID_ROM;}
  std::ifstream f(path,std::ios::binary);std::array<unsigned char,0x200> h{};f.read((char*)h.data(),h.size());
  if(f.gcount()<0x150){error="Cannot read ROM header";return EF_ERROR_IO;}
  const bool gb_logo=h[0x104]==0xCE&&h[0x105]==0xED&&h[0x106]==0x66&&h[0x107]==0x66;
  const bool gba_logo=h[0x03]==0xEA&&h[0xB2]==0x96;
  EF_System system=EF_SYSTEM_AUTO;size_t title_offset=0,title_len=0;
  if(gba_logo){system=EF_SYSTEM_GBA;title_offset=0xA0;title_len=12;}
  else if(gb_logo){system=(h[0x143]==0x80||h[0x143]==0xC0)?EF_SYSTEM_GBC:EF_SYSTEM_GB;title_offset=0x134;title_len=system==EF_SYSTEM_GBC?11:16;}
  else {error="Unrecognized GB/GBC/GBA ROM header";return EF_ERROR_INVALID_ROM;}
  auto hash=sha256_file(path);if(hash.empty()){error="Cannot hash ROM";return EF_ERROR_IO;}
  out={};out.path=path;out.info.system=system;out.info.rom_size=size;
  auto title=clean_title(h.data()+title_offset,title_len);if(title.empty())title=path.stem().string();
  copy(out.info.title,sizeof(out.info.title),title);
  copy(out.info.sha256,sizeof(out.info.sha256),hash);
  auto utf8=path.u8string();copy(out.info.loaded_path,sizeof(out.info.loaded_path),std::string(utf8.begin(),utf8.end()));
  copy(out.info.save_type,sizeof(out.info.save_type),"auto (mGBA)");
  return EF_OK;
}
}
