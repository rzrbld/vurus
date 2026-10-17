#include "assets/gaf_audio.h"
#include <cstdlib>
#include <iostream>
using Bytes=std::vector<uint8_t>;
void check(bool ok,const char* text){if(!ok){std::cerr<<text<<'\n';std::exit(1);}}
void put(Bytes& b,size_t off,uint32_t v){for(int i=0;i<4;++i)b[off+i]=v>>(8*i);}
void append(Bytes& b,const Bytes& c){b.insert(b.end(),c.begin(),c.end());}
Bytes chunk(const char* tag,Bytes payload,uint32_t count=0){Bytes b(16);for(int i=0;i<4;++i)b[i]=tag[i];put(b,4,payload.size());put(b,12,count);append(b,payload);return b;}
Bytes fixture(uint32_t count=1,uint32_t frames=3,uint32_t bits=16,bool omit=false){
    Bytes snd(44);snd[0]='S';snd[1]='N';snd[2]='D';snd[3]='_';put(snd,4,8);put(snd,12,24);put(snd,20,22050);put(snd,24,bits);put(snd,28,1);put(snd,32,frames);snd[36]=0xff;snd[37]=0x7f;snd[42]=0xaa;snd[43]=0xbb;
    Bytes stream(96+76);put(stream,16,76);put(stream,40,snd.size());append(stream,snd);
    Bytes hdr(36);put(hdr,0,count);
    Bytes head=omit?Bytes{}:chunk("SMP_",hdr),data=omit?Bytes{}:chunk("SMP_",stream);
    // HEAD/DATA without samples still have other children in real banks.
    if(omit){head=chunk("GOB_",Bytes{0});data=chunk("GOB_",Bytes{0});}
    Bytes root=chunk("HEAD",head,1);append(root,chunk("DATA",data,1));return chunk("GAF_",root,2);
}
int main(){
    std::vector<vyruz::Wave> waves;std::string error;
    check(vyruz::decodeGafSamples(fixture(),waves,error),"decode embedded PCM sample");
    check(waves.size()==1&&waves[0].sampleRate==22050&&waves[0].bitsPerSample==16&&waves[0].channels==1,"preserve sample metadata");
    check(waves[0].pcm.size()==6&&waves[0].pcm[0]==255&&waves[0].pcm[1]==127,"strip alignment padding without changing PCM");
    check(!vyruz::decodeGafSamples(fixture(2),waves,error),"reject mismatched sample count");
    check(waves.size()==1&&waves[0].pcm.size()==6,"failed decode leaves previous result intact");
    check(!vyruz::decodeGafSamples(fixture(1,5),waves,error),"reject frames beyond stored sample bytes");
    check(!vyruz::decodeGafSamples(fixture(1,3,24),waves,error),"reject unsupported PCM depth");
    auto truncated=fixture();truncated.pop_back();check(!vyruz::decodeGafSamples(truncated,waves,error),"reject truncated bank");
    auto trailing=fixture();trailing.push_back(0);check(!vyruz::decodeGafSamples(trailing,waves,error),"reject trailing bank bytes");
    check(vyruz::decodeGafSamples(fixture(1,3,16,true),waves,error)&&waves.empty(),"bank without embedded sound succeeds empty");
}
