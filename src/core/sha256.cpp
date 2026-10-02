#include "rom.hpp"
#include <algorithm>
#include <array>
#include <fstream>
#include <iomanip>
#include <sstream>
namespace ef {
namespace {
constexpr uint32_t k[64] = {
  0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
  0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
  0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
  0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
  0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
  0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
  0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
  0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
uint32_t rotr(uint32_t x, unsigned n) { return (x>>n)|(x<<(32-n)); }
struct Hash {
  uint32_t h[8]={0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
  uint8_t block[64]{}; size_t used=0; uint64_t bytes=0;
  void transform() {
    uint32_t w[64]{};
    for(int i=0;i<16;i++) w[i]=(uint32_t(block[i*4])<<24)|(uint32_t(block[i*4+1])<<16)|(uint32_t(block[i*4+2])<<8)|block[i*4+3];
    for(int i=16;i<64;i++) { auto s0=rotr(w[i-15],7)^rotr(w[i-15],18)^(w[i-15]>>3); auto s1=rotr(w[i-2],17)^rotr(w[i-2],19)^(w[i-2]>>10); w[i]=w[i-16]+s0+w[i-7]+s1; }
    uint32_t a=h[0],b=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],z=h[7];
    for(int i=0;i<64;i++) { auto s1=rotr(e,6)^rotr(e,11)^rotr(e,25); auto t1=z+s1+((e&f)^((~e)&g))+k[i]+w[i]; auto s0=rotr(a,2)^rotr(a,13)^rotr(a,22); auto t2=s0+((a&b)^(a&c)^(b&c)); z=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+t2; }
    h[0]+=a;h[1]+=b;h[2]+=c;h[3]+=d;h[4]+=e;h[5]+=f;h[6]+=g;h[7]+=z;
  }
  void update(const uint8_t* p,size_t n) { bytes+=n; while(n) { auto count=std::min(n,64-used); std::copy_n(p,count,block+used);p+=count;n-=count;used+=count;if(used==64){transform();used=0;} } }
  std::string finish() { auto bits=bytes*8; block[used++]=0x80; if(used>56){while(used<64)block[used++]=0;transform();used=0;}while(used<56)block[used++]=0;for(int i=7;i>=0;i--)block[used++]=uint8_t(bits>>(i*8));transform();std::ostringstream os;os<<std::hex<<std::setfill('0');for(auto x:h)os<<std::setw(8)<<x;return os.str(); }
};
}
std::string sha256_file(const std::filesystem::path& path) {
  std::ifstream f(path,std::ios::binary); if(!f)return {};
  Hash hash; std::array<uint8_t,65536> buffer{};
  while(f) { f.read(reinterpret_cast<char*>(buffer.data()),buffer.size()); auto n=f.gcount(); if(n>0)hash.update(buffer.data(),size_t(n)); }
  return f.bad()?std::string{}:hash.finish();
}
}
