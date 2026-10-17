#include "game/actor_ammo_contact_native.h"
#include "game/native_actor_cells.h"
#include "game/spider_contact_native.h"
#include <array>
#include <bit>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace vyruz;
namespace {
using Word=std::uint32_t;
using Record=std::array<Word,64>;
Word bits(float value){return std::bit_cast<Word>(value);}
void need(bool ok,const char* what){if(!ok)throw std::runtime_error(what);}
void hashRecord(std::uint64_t& hash,const Record& record){
    for(const Word word:record)for(unsigned shift=0;shift<32;shift+=8){
        hash^=(word>>shift)&0xffu;hash*=1099511628211ull;
    }
}
}

// Original PE44175b..441e1f, optional44169a rest and second no-hit
//441d11..442aa2 records are retained in n2-continuation/native. Query,
//impact and graph-repair dependencies are declared stubs there. This test
//checks the velocity helper, exact native projections and linked-cell producer;
//World callers, actual damage and the next AI/438b20 decision are separate tests.
int main(int argc,char** argv){try{
    need(argc==1||argc==2,"usage: actor_ammo_continuation_native_test [NATIVE_RECORDS]");
    std::ifstream in;
    if(argc==2){in.open(argv[1],std::ios::binary);need(bool(in),"native records missing");}
    std::uint64_t fingerprint=14695981039346656037ull;
    unsigned count=0,failed=0,aborts=0,unscaled=0,provisional=0,reentries=0;
    for(Word subtype=0;subtype<64;++subtype)
    for(Word behavior=0;behavior<32;++behavior)
    for(Word movement=0;movement<3;++movement)
    for(Word origin=0;origin<2;++origin)
    for(Word rate=0;rate<2;++rate){
        Record actual{};
        actual[0]=subtype;actual[1]=behavior;actual[2]=movement;actual[3]=origin;actual[4]=rate;
        const Displacement initialSurface{origin?4000.f:1600.125f,origin?3000.f:1000.375f};
        const auto old=spiderSurfaceToNative(initialSurface.x,initialSurface.y);
        const ContactVelocity step{movement==0?20.125f:movement==1?-20.125f:.000001f,
                                   movement==0?-7.375f:movement==1?20.5f:0.f};
        const float factor=rate?1.26f:.84f;
        const ContactVelocity trial{float(static_cast<long double>(old.x)+step.x),
                                    float(static_cast<long double>(old.y)+step.z)};
        const auto proposedSurface=spiderNativeToSurface(trial.x,trial.z);
        const auto proposed=spiderSurfaceToNative(proposedSurface.x,proposedSurface.y);
        const auto previousSurface=spiderNativeToSurface(old.x,old.y);
        // The complete caller reaches downstream non-Ammo/physical boundary0
        //for excluded source subtypes. Its impacted sources reach boundary3.
        const bool abort=((Word(1)<<((subtype-0x10u)&31u))&0x1fu)==0;
        const auto velocity=nativeActorAmmoAbortVelocity(subtype,behavior,
            {old.x,old.y},{proposed.x,proposed.y},step,factor);
        actual[5]=bits(old.x);actual[6]=bits(old.y);actual[7]=abort?3:0;
        actual[8]=bits(velocity.x);actual[9]=bits(velocity.z);
        actual[10]=actual[5];actual[11]=actual[6];
        actual[12]=bits(proposed.x);actual[13]=bits(proposed.y);
        actual[14]=bits(proposedSurface.x);actual[15]=bits(proposedSurface.y);
        actual[16]=bits(previousSurface.x);actual[17]=bits(previousSurface.y);
        actual[18]=static_cast<Word>(proposedSurface.x)>>6;
        actual[19]=static_cast<Word>(proposedSurface.y)>>5;
        actual[20]=bits(789.f);actual[21]=bits(1011.f);actual[22]=200;actual[23]=208;
        actual[24]=abort?18:17;actual[25]=0x08000000u|(abort?0x200u:0u);
        actual[26]=actual[27]=actual[28]=abort?1:0;actual[29]=1;
        actual[30]=static_cast<Word>(previousSurface.x)>>6;
        actual[31]=static_cast<Word>(previousSurface.y)>>5;
        // A second equal supplied step starts from the retained old center.
        //It repeats the proposed position and commits its rebuilt native bounds.
        actual[32]=4;
        const bool clearSmallFamily=subtype==0x19||subtype==0x1f||subtype==0x20;
        actual[33]=bits(clearSmallFamily?0.f:float(static_cast<long double>(factor)*step.x));
        actual[34]=bits(clearSmallFamily?0.f:float(static_cast<long double>(factor)*step.z));
        actual[35]=actual[37]=actual[12];actual[36]=actual[38]=actual[13];
        actual[39]=actual[14];actual[40]=actual[15];
        actual[41]=actual[16];actual[42]=actual[17];
        actual[43]=actual[18];actual[44]=actual[19];
        actual[45]=actual[20];actual[46]=actual[21];actual[47]=200;actual[48]=208;
        const bool crossed=actual[18]!=actual[30]||actual[19]!=actual[31];
        // Two representative cells suffice: exact actual cell coordinates are
        //retained above. Inserting the peer between visits distinguishes the
        //native old-cell re-entry from synchronizing only the final cell.
        NativeActorCells cells;cells.reset(1,2);cells.relocate(0,0);
        cells.relocate(0,crossed?1:0);
        if(crossed)cells.relocate(1,1);
        actual[50]=crossed;actual[51]=cells.heads[crossed?1:0]==1;
        cells.relocate(0,0);cells.relocate(0,crossed?1:0);
        actual[49]=cells.heads[crossed?1:0]==0;
        actual[52]=cells.next[0]==1;
        need(cells.valid(crossed?2:1),"continuation cell producer invalid");
        // Separate managed zero-step44169a visit, sampled after the first
        //visit then restored before the moving continuation in the probe.
        //Rest copies current surface to previous without committing old bounds.
        actual[53]=4;actual[54]=actual[5];actual[55]=actual[6];
        actual[56]=actual[58]=actual[14];actual[57]=actual[59]=actual[15];
        actual[60]=actual[61]=0;actual[62]=actual[20];actual[63]=200;
        if(argc==2){Record native{};need(bool(in.read(reinterpret_cast<char*>(native.data()),sizeof native)),"short native records");
            if(native!=actual){if(failed<4){std::cerr<<"mismatch subtype="<<subtype<<" behavior="<<behavior<<" movement="<<movement<<" origin="<<origin<<" rate="<<rate;
                for(size_t n=0;n<actual.size();++n)if(native[n]!=actual[n])std::cerr<<" word="<<n<<" native="<<native[n]<<" port="<<actual[n];std::cerr<<'\n';}++failed;}}
        hashRecord(fingerprint,actual);++count;reentries+=crossed;
        if(abort){++aborts;const bool rawUnscale=((Word(1)<<((subtype-0x10u)&31u))&0x1eu)==0&&
            ((behavior&0x1cu)==0xcu||(behavior&3u)==1u||(behavior&3u)==2u);
            rawUnscale?++unscaled:++provisional;}
    }
    if(argc==2){char extra;need(!in.read(&extra,1),"extra native records");}
    need(count==24576&&aborts==20736&&unscaled==11664&&provisional==9072&&reentries==8192,"native coverage count");
    // FNV-1a of original result.bin, derived from original captured bytes;
    //not from this helper or expected-state reconstruction.
    need(fingerprint==0x43d2b746d92cd215ull,"original continuation fingerprint mismatch");
    need(failed==0,"original continuation record mismatch");
    std::cout<<count<<" original abort/continuation/rest records; "<<aborts<<" impact-aborts, "<<reentries<<" intermediate cell re-entries, "<<failed<<" mismatches\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
