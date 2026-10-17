#include "assets/intro_video.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <fstream>
#include <iterator>
#include <iostream>
#include <chrono>
#include <cstring>
using Bytes=std::vector<uint8_t>;
void put(Bytes& b,size_t p,uint32_t n){for(int i=0;i<4;++i)b[p+i]=n>>(8*i);}
Bytes chunk(const char* name,Bytes data){Bytes b(8);std::memcpy(b.data(),name,4);put(b,4,data.size());b.insert(b.end(),data.begin(),data.end());if(data.size()&1)b.push_back(0);return b;}
void add(Bytes& out,const Bytes& b){out.insert(out.end(),b.begin(),b.end());}
Bytes list(const char* kind,Bytes data){Bytes b(kind,kind+4);add(b,data);return chunk("LIST",b);}
Bytes fixture(){
 Bytes h(56),f(40);std::memcpy(h.data(),"vidsiv50",8);put(h,20,1);put(h,24,30);put(h,32,1);
 put(f,0,40);put(f,4,320);put(f,8,180);std::memcpy(f.data()+16,"IV50",4);
 Bytes stream;add(stream,chunk("strh",h));add(stream,chunk("strf",f));
 Bytes payload{'A','V','I',' '};add(payload,list("hdrl",list("strl",stream)));
 add(payload,list("movi",chunk("00db",{0x1f})));return chunk("RIFF",payload);
}
int main(int argc,char** argv){
 vyruz::IntroVideo movie;std::string error;vyruz::Image frame;
 auto good=fixture();assert(movie.load(good,error));assert(movie.width()==320&&movie.height()==180&&movie.frameCount()==1);
 assert(movie.frameRate()==30&&movie.durationMilliseconds()>33&&movie.durationMilliseconds()<34);
 assert(!movie.frame(0,frame,error)&&!error.empty()&&frame.rgba.empty()); // incomplete codec packet
 assert(!movie.frame(1,frame,error));
 for(size_t size=0;size<good.size();++size){Bytes shortFile(good.begin(),good.begin()+size);assert(!movie.load(shortFile,error)&&movie.frameCount()==0);}
 auto bad=good;put(bad,4,0xffffffff);assert(!movie.load(bad,error));
 bad=good;put(bad,16,0xffffffff);assert(!movie.load(bad,error));
 bad=good;for(size_t i=0;i+4<bad.size();++i)if(std::memcmp(bad.data()+i,"IV50",4)==0)bad[i]='X';assert(!movie.load(bad,error));
 bad=good;bad[bad.size()-2]=0x3f;assert(!movie.load(bad,error)); // INTER unsupported
 assert(movie.load(good,error)&&error.empty());
 if(argc>1){
  std::ifstream input(argv[1],std::ios::binary);assert(input);Bytes actual((std::istreambuf_iterator<char>(input)),{});
  assert(movie.load(actual,error));assert(movie.width()==320&&movie.height()==180&&movie.frameCount()==3090&&movie.duration()==103);
  assert(movie.audio().sampleRate==22050&&movie.audio().channels==2&&movie.audio().bitsPerSample==16&&movie.audio().pcm.size()==9084600);
  const auto start=std::chrono::steady_clock::now();uint64_t hash=14695981039346656037ULL;
  for(size_t i=0;i<movie.frameCount();++i){if(!movie.frame(i,frame,error)){std::cerr<<"Frame "<<i<<": "<<error<<'\n';return 1;}for(auto byte:frame.rgba){hash^=byte;hash*=1099511628211ULL;}}
  assert(hash==0x38baa54c7272d98aULL);
  std::cout<<"All3090 frames; RGBA FNV1a "<<std::hex<<hash<<std::dec<<"; seconds "<<std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count()<<'\n';
 }
}
