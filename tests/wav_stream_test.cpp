#include "audio/mixer.h"
#include "audio/resample_kernel.h"
#include "audio/music_playback.h"
#include "host_temp.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <thread>
using namespace vyruz;
static void need(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
static void integer(std::ofstream& f,uint32_t x,int n){for(int i=0;i<n;++i)f.put(char(x>>(8*i)));}
static void write(const std::filesystem::path& p,const Wave& w){
 std::ofstream f(p,std::ios::binary);f.write("RIFF",4);integer(f,uint32_t(w.pcm.size()+36),4);f.write("WAVEfmt ",8);integer(f,16,4);integer(f,1,2);integer(f,w.channels,2);integer(f,w.sampleRate,4);integer(f,w.sampleRate*w.channels*(w.bitsPerSample/8),4);integer(f,w.channels*(w.bitsPerSample/8),2);integer(f,w.bitsPerSample,2);f.write("data",4);integer(f,uint32_t(w.pcm.size()),4);f.write(reinterpret_cast<const char*>(w.pcm.data()),w.pcm.size());
}
static AudioClipPtr streamed(const std::filesystem::path&p){
 std::string error;auto s=WavStream::open(p.string(),error);need(bool(s),error.c_str());
 auto c=std::make_shared<AudioClip>();c->wave.sampleRate=s->rate();c->wave.channels=s->channels();c->wave.bitsPerSample=s->bits();c->frames=s->frames();c->stream=s;return c;
}
static void waitFrame(WavStream& s,uint64_t frame){
 float out[2];for(int i=0;i<2000&&!s.frame(frame,out);++i)std::this_thread::sleep_for(std::chrono::milliseconds(1));
 need(s.frame(frame,out),"stream refill timeout");
}
int main(){
 const auto path=std::filesystem::temp_directory_path()/("vurus-stream-"+std::to_string(getpid())+".wav");
 for(bool quality:{false,true})for(auto bits:{8,16})for(auto channels:{1,2}){
  Wave wave;wave.bitsPerSample=bits;wave.channels=channels;wave.sampleRate=44100;
  wave.pcm.resize(70000*channels*(bits/8));for(size_t i=0;i<wave.pcm.size();++i)wave.pcm[i]=uint8_t(i*13+41);
  write(path,wave);std::string error;auto resident=makeAudioClip(wave,error),stream=streamed(path);
  need(stream->wave.pcm.empty()&&stream->stream->bufferBytes()==65536u*channels*(bits/8),"bounded PCM independent of file size");
  AudioMixer a,b;a.configureMix({false,quality});b.configureMix({false,quality});a.playMusic(resident,.2f,true);b.playMusic(stream,.2f,true);
  for(size_t frame=0;frame<180000;frame+=256){
   waitFrame(*stream->stream,uint64_t((frame+256)*44100./48000)+(quality?AudioResampleKernel::MaxTaps/2+2:1));
   float x[512],y[512];a.mix(x,256);b.mix(y,256);
   for(int i=0;i<512;++i)need(x[i]==y[i],"stream/resident PCM differ across refill or loop");
   if(frame==512){a.setPaused(true);b.setPaused(true);a.mix(x,256);b.mix(y,256);for(int i=0;i<512;++i)need(x[i]==0&&y[i]==0,"pause silence");a.setPaused(false);b.setPaused(false);}
  }
 }
 // AVI-style PCM packets separated by non-audio bytes; interpolate through
 // packet boundaries, ring refill and wrap without exposing the gap bytes.
 {
  Wave wave;wave.sampleRate=48000;wave.channels=2;wave.bitsPerSample=16;
  std::vector<PcmChunk> chunks;std::ofstream f(path,std::ios::binary);uint64_t offset=0;
  for(unsigned part=0;part<1000;++part){
   const unsigned bytes=4*(31+part%83);f.write("GAP!",4);offset+=4;chunks.push_back({offset,bytes});
   for(unsigned j=0;j<bytes;++j){const auto value=uint8_t(part+j*7);f.put(char(value));wave.pcm.push_back(value);}offset+=bytes;
  }f.close();std::string error;
  auto source=WavStream::openPcm(path.string(),48000,2,16,chunks,error);need(bool(source),error.c_str());
  auto stream=std::make_shared<AudioClip>();stream->wave.sampleRate=48000;stream->wave.channels=2;stream->wave.bitsPerSample=16;stream->frames=source->frames();stream->stream=source;
  AudioMixer a,b;a.playMusic(makeAudioClip(wave,error),.2f,true);b.playMusic(stream,.2f,true);
  for(uint64_t start=0;start<160000;start+=256){waitFrame(*source,start+256);float x[512],y[512];a.mix(x,256);b.mix(y,256);for(int j=0;j<512;++j)need(x[j]==y[j],"segmented PCM mismatch");}
  chunks.front().bytes=3;need(!WavStream::openPcm(path.string(),48000,2,16,chunks,error),"partial indexed sample rejected");
 }
 // Exact end-of-track handoff, including a callback spanning two tracks.
 Wave tiny;tiny.sampleRate=48000;tiny.channels=1;tiny.bitsPerSample=8;tiny.pcm.assign(101,145);tiny.pcm.push_back(143);write(path,tiny);
 auto one=streamed(path),two=streamed(path);AudioMixer m;m.playMusic(one,.2f,false);m.queueMusic(two,.2f);float out[1024];m.mix(out,512);
 need(m.musicState().serial==2&&!m.musicState().playing,"gapless queued track consumed at exact boundary");
 m.playMusic({});one.reset();two.reset();
 {
  SDL_setenv("SDL_AUDIODRIVER","dummy",1);AudioDevice device;std::string error;need(device.open(error),"dummy device");device.setPaused(true);
  MusicPlayback music(device);need(music.musicFile(path.string(),error),error.c_str());
  for(int repeat=0;repeat<4;++repeat){
   music.setOptionEnabled(false);need(!device.musicState().playing,"fixed stream OFF");music.setOptionEnabled(true);
   for(int i=0;i<2000&&!device.musicState().playing;++i){music.update();SDL_Delay(1);}
   need(device.musicState().playing&&device.musicState().milliseconds==0,"fixed stream reopen while paused");
  }
  music.setOptionEnabled(false);device.close();
 }
 // Bad files are rejected before a worker or live playback is published.
 std::filesystem::resize_file(path,20);std::string error;need(!WavStream::open(path.string(),error)&&!error.empty(),"truncated file rejected");
 std::filesystem::remove(path);
}
