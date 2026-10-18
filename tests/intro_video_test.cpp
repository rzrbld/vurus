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
#include <filesystem>
#include <thread>
#include "host_temp.h"
using Bytes=std::vector<uint8_t>;
void put(Bytes& b,size_t p,uint32_t n){for(int i=0;i<4;++i)b[p+i]=n>>(8*i);}
Bytes chunk(const char* name,Bytes data){Bytes b(8);std::memcpy(b.data(),name,4);put(b,4,data.size());b.insert(b.end(),data.begin(),data.end());if(data.size()&1)b.push_back(0);return b;}
void add(Bytes& out,const Bytes& b){out.insert(out.end(),b.begin(),b.end());}
Bytes list(const char* kind,Bytes data){Bytes b(kind,kind+4);add(b,data);return chunk("LIST",b);}
Bytes fixture(size_t count=1,size_t packetSize=1){
 Bytes h(56),f(40);std::memcpy(h.data(),"vidsiv50",8);put(h,20,1);put(h,24,30);put(h,32,count);
 put(f,0,40);put(f,4,320);put(f,8,180);std::memcpy(f.data()+16,"IV50",4);
 Bytes stream;add(stream,chunk("strh",h));add(stream,chunk("strf",f));
 Bytes payload{'A','V','I',' '};add(payload,list("hdrl",list("strl",stream)));
 Bytes packets;Bytes packet(packetSize);packet[0]=0x1f;
 for(size_t i=0;i<count;++i)add(packets,chunk("00db",packet));
 add(payload,list("movi",packets));return chunk("RIFF",payload);
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
 const auto path=std::filesystem::temp_directory_path()/("vurus-intro-stream-"+std::to_string(getpid())+".avi");
 {std::ofstream f(path,std::ios::binary);f.write(reinterpret_cast<const char*>(good.data()),good.size());}
 assert(movie.loadFile(path.string(),error)&&movie.frameCount()==1&&movie.retainedMediaBytes()==0);
 assert(!movie.frame(0,frame,error)&&frame.rgba.empty());movie={};
 {std::ofstream f(path,std::ios::binary);f.write(reinterpret_cast<const char*>(good.data()),good.size()-1);}
 assert(!movie.loadFile(path.string(),error)&&movie.frameCount()==0);movie={};std::filesystem::remove(path);
 // A movie larger than the budget must fill only 512KiB, and support seeks
 // outside that window and backwards without retaining old packet queues.
 const auto large=fixture(400,4096);
 {std::ofstream f(path,std::ios::binary);f.write(reinterpret_cast<const char*>(large.data()),large.size());}
 assert(movie.loadFile(path.string(),error));
 for(int i=0;i<1000&&movie.videoReadAheadBytes()<512*1024;++i)std::this_thread::sleep_for(std::chrono::milliseconds(1));
 assert(movie.videoReadAheadBytes()==512*1024);
 for(size_t i:{size_t(0),size_t(250),size_t(251),size_t(0),size_t(399)}){
  assert(!movie.frame(i,frame,error)); // intentionally invalid compressed payload
  assert(movie.videoReadAheadBytes()<=512*1024);
 }
 movie={};assert(movie.videoReadAheadBytes()==0);
 assert(movie.loadFile(path.string(),error));
 for(int i=0;i<1000&&movie.videoReadAheadBytes()<512*1024;++i)std::this_thread::sleep_for(std::chrono::milliseconds(1));
 std::filesystem::resize_file(path,64);
 assert(!movie.frame(399,frame,error)&&error=="Truncated AVI read-ahead");
 movie={};std::filesystem::remove(path);
 if(argc>1){
  assert(movie.loadFile(argv[1],error)&&movie.retainedMediaBytes()==0&&movie.indexBytes()<256*1024);assert(movie.width()==320&&movie.height()==180&&movie.frameCount()==3090&&movie.duration()==103);
  assert(movie.audio().sampleRate==22050&&movie.audio().channels==2&&movie.audio().bitsPerSample==16&&movie.audio().pcm.empty());
  uint64_t audioBytes=0;for(const auto& c:movie.audioChunks())audioBytes+=c.bytes;assert(audioBytes==9084600);
  const auto start=std::chrono::steady_clock::now();uint64_t hash=14695981039346656037ULL;
  for(size_t i=0;i<movie.frameCount();++i){if(!movie.frame(i,frame,error)){std::cerr<<"Frame "<<i<<": "<<error<<'\n';return 1;}for(auto byte:frame.rgba){hash^=byte;hash*=1099511628211ULL;}}
  assert(hash==0x38baa54c7272d98aULL);
  vyruz::Image reference,repeated;
  assert(movie.frame(1500,reference,error));
  assert(movie.frame(0,repeated,error));
  assert(movie.frame(1500,repeated,error)&&reference.rgba==repeated.rgba);
  assert(movie.frame(1500,repeated,error)&&reference.rgba==repeated.rgba);
  assert(movie.videoReadAheadBytes()<=512*1024);
  std::cout<<"All3090 frames; RGBA FNV1a "<<std::hex<<hash<<std::dec<<"; seconds "<<std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count()<<'\n';
 }
}
