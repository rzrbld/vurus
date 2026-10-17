#include "game/demo_replay.h"
#include <cassert>
#include <fstream>
#include <iterator>

static void append(std::vector<uint8_t>& bytes,uint32_t word) {
    for(int i=0;i<4;++i)bytes.push_back(static_cast<uint8_t>(word>>(8*i)));
}
static void record(std::vector<uint8_t>& bytes,uint32_t time,uint32_t flags,float x,unsigned direction) {
    append(bytes,time);append(bytes,flags);append(bytes,std::bit_cast<uint32_t>(x));
    append(bytes,std::bit_cast<uint32_t>(10.f));append(bytes,direction);
}
int main(int argc,char** argv) {
    std::vector<uint8_t> bytes;append(bytes,4);
    record(bytes,4606,0x10000,1,0);record(bytes,4606,2,2,1);
    record(bytes,4622,0x20004,3,2);record(bytes,4640,0x8000,4,3);
    vyruz::DemoReplay replay;std::string error;
    assert(replay.load(bytes,error));assert(replay.recordCount()==4);
    auto frame=replay.advance(0);assert(frame.pose->x==2 && frame.fire());
    assert(frame.weaponSlot==0 && !frame.ended);
    frame=replay.advance(10);assert(frame.flags==0 && frame.pose->x==2);
    frame=replay.advance(6);assert(frame.weaponSlot==1 && frame.pose->x==3);
    frame=replay.advance(18);assert(frame.ended && frame.pose->direction==3);
    replay.reset();frame=replay.advance(34);assert(frame.ended && frame.weaponSlot==1);
    auto bad=bytes;bad.pop_back();assert(!replay.load(bad,error));
    bad=bytes;bad[20]=24;assert(!replay.load(bad,error));
    bad=bytes;bad[24]=0;bad[25]=0;assert(!replay.load(bad,error));
    bad=bytes;bad[12]=0;bad[13]=0;bad[14]=0x80;bad[15]=0x7f;
    assert(!replay.load(bad,error));
    if(argc>1) {
        std::ifstream input(argv[1],std::ios::binary);assert(input);
        std::vector<uint8_t> original{std::istreambuf_iterator<char>(input),{}};
        assert(replay.load(original,error));assert(replay.recordCount()==32283);
        assert(replay.durationMilliseconds()==270620);
        frame=replay.advance(0);assert(frame.pose->x==2157 && frame.pose->y==1095);
        assert(frame.pose->direction==0 && frame.weaponSlot==0);
        frame=replay.advance(270620);assert(frame.ended && (frame.flags&0x8000));
        assert(frame.pose->direction==12);
    }
}
