#include "assets/wav.h"
#include <cstdlib>
#include <iostream>
using namespace vyruz;
void check(bool value) { if (!value) { std::cerr << "WAV check failed\n"; std::exit(1); } }
void put16(std::vector<uint8_t>& b, size_t p, unsigned n) { b[p] = n; b[p+1] = n >> 8; }
void put32(std::vector<uint8_t>& b, size_t p, unsigned n) { put16(b,p,n); put16(b,p+2,n >> 16); }
std::vector<uint8_t> valid() {
    std::vector<uint8_t> b(48);
    for (auto [p,s] : {std::pair{0,"RIFF"}, {8,"WAVE"}, {12,"fmt "}, {36,"data"}})
        for (int i=0;i<4;++i) b[p+i]=s[i];
    put32(b,4,40); put32(b,16,16); put16(b,20,1); put16(b,22,1);
    put32(b,24,22050); put32(b,28,44100); put16(b,32,2); put16(b,34,16); put32(b,40,4);
    b[45]=128; b[46]=255; b[47]=127;
    return b;
}
int main() {
    Wave w; auto b=valid(); check(loadWav(b,w)); check(w.pcm.size()==4 && w.sampleRate==22050);
    const auto original=w.pcm;
    for (size_t n=0;n<b.size();++n) {
        std::vector<uint8_t> truncated(b.begin(),b.begin()+n);
        check(!loadWav(truncated,w)); check(w.pcm==original && w.sampleRate==22050);
    }
    for (auto [p,value] : {std::pair{20,3u},{22,3u},{24,0u},{28,1u},{32,1u},{34,24u},{40,0xffffffffu},{4,0xffffffffu},{40,3u}}) {
        auto malformed=b; put32(malformed,p,value); check(!loadWav(malformed,w)); check(w.pcm==original);
    }
    // An odd unknown chunk with its required pad is skipped.
    auto extra=b; extra.insert(extra.begin()+36,10,0); extra[36]='J';extra[37]='U';extra[38]='N';extra[39]='K';
    put32(extra,40,1); put32(extra,4,50); check(loadWav(extra,w));
    // Chunk ordering need not put format before PCM.
    auto reversed=b; std::copy(b.begin()+36,b.end(),reversed.begin()+12);
    std::copy(b.begin()+12,b.begin()+36,reversed.begin()+24); check(loadWav(reversed,w));
    std::cout << "WAV validation passed\n";
}
