#include "game/player_facing_native.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <fstream>

static void check(bool ok,const char* description) {
    if(!ok){std::fprintf(stderr,"Player facing: %s\n",description);std::exit(1);}
}
int main(int argc,char** argv) {
    using namespace vyruz;
    check(nativePlayerTargetAction(0,12)==0xd0,"opposite is reverse propulsion");
    check(nativePlayerTargetAction(0,12,false)==0xcb,"opposite option disabled");
    check(nativePlayerAimTurnCommand(0,12,false,false,false)==0,"opposite does not turn");
    check(nativePlayerAimTurnCommand(0,1,false,false,false)==0xcb,"stationary adjacent mouse turn");
    check(nativePlayerAimTurnCommand(0,1,true,false,false)==0,"moving adjacent mouse deadband");
    check(nativePlayerAimTurnCommand(0,23,false,true,false)==0xcb,"reverse commands swap turn");
    check(nativePlayerAimTurnCommand(0,2,false,false,true)==0,"blocked motor clears turn");
    auto state=initializeNativePlayerFacing(0);
    advanceNativePlayerFacing(state,0xcb,18);
    check(state.logicalHeading==1&&state.pose==1&&state.desiredPose==3&&!nativePlayerFacingRested(state),"first command commits node before pose");
    advanceNativePlayerFacing(state,0xcb,18);
    check(state.pose==2&&!nativePlayerFacingRested(state),"intermediate pose prevents rest");
    advanceNativePlayerFacing(state,0xcb,18);
    check(state.pose==3&&state.logicalHeading==1&&nativePlayerFacingRested(state),"arrival before next commit");
    advanceNativePlayerFacing(state,0xcb,18);
    check(state.logicalHeading==2&&state.pose==3&&state.desiredPose==5,"next commit exact18ms schedule");
    advanceNativePlayerFacing(state,0,18);
    check(state.pose==4&&!nativePlayerFacingRested(state),"release completes pending node");
    advanceNativePlayerFacing(state,0,18);
    check(state.pose==5&&nativePlayerFacingRested(state),"release arrival");
    const auto idleHeading=state.logicalHeading;
    advanceNativePlayerFacing(state,0,11);
    check(state.remainingMs==7&&state.logicalHeading==idleHeading,"idle countdown cycles");
    advanceNativePlayerFacing(state,0xcd,14);
    check(state.logicalHeading==1&&state.desiredPose==3,"reverse command commits after remainder");
    auto wrap=initializeNativePlayerFacing(23);
    advanceNativePlayerFacing(wrap,0xcb,18);
    advanceNativePlayerFacing(wrap,0,18);
    check(wrap.pose==48,"positive wrap halfpose");
    advanceNativePlayerFacing(wrap,0,18);
    check(wrap.pose==1&&wrap.logicalHeading==0,"positive wrap arrival");

    if(argc>1) {
        std::ifstream in(argv[1],std::ios::binary);std::uint32_t count=0;
        in.read(reinterpret_cast<char*>(&count),4);check(bool(in)&&count==138240,"oracle header");
        for(std::uint32_t i=0;i<count;++i) {
            std::array<std::int32_t,17> row;in.read(reinterpret_cast<char*>(row.data()),sizeof(row));check(bool(in),"oracle record");
            auto s=initializeNativePlayerFacing(row[0],row[16]);
            s.pose=row[1];s.desiredPose=row[2];s.previousPose=row[3];s.remainingMs=row[4];s.fastTurn=row[7];
            advanceNativePlayerFacing(s,row[5],row[6]);
            if(s.logicalHeading!=row[8]||s.pose!=row[9]||s.desiredPose!=row[10]||
               s.previousPose!=row[11]||s.remainingMs!=row[12]||s.fastTurn!=bool(row[13])||
               s.orthogonalHeading!=row[14]||nativePlayerFacingRested(s)!=bool(row[15])) {
                std::fprintf(stderr,"oracle mismatch record%u initial h/p/t/r/c/d/f=%d/%d/%d/%d/%d/%d/%d actual p/t/r=%d/%d/%d expected=%d/%d/%d\n",i,row[0],row[1],row[2],row[4],row[5],row[6],row[7],s.pose,s.desiredPose,s.remainingMs,row[9],row[10],row[12]);return 1;
            }
        }
        check(in.peek()==std::char_traits<char>::eof(),"oracle trailing bytes");
        std::puts("138240 whole original443990/438390 player-facing records match");
    }
    if(argc>2) {
        std::ifstream in(argv[2],std::ios::binary);std::uint32_t count=0;
        in.read(reinterpret_cast<char*>(&count),4);check(bool(in)&&count==5000,"command oracle header");
        for(std::uint32_t i=0;i<count;++i) {
            std::array<std::int32_t,9> row;in.read(reinterpret_cast<char*>(row.data()),sizeof(row));check(bool(in),"command oracle record");
            check(nativePlayerTargetAction(row[0],row[1])==row[5],"whole436600 raw classifier");
            check(nativePlayerMouseAction(row[0],row[1],row[2])==row[6],"moving mouse deadband");
            check(nativePlayerAimTurnCommand(row[0],row[1],row[2],row[3],row[4])==row[7],"whole438540 reverse/motor admission");
            check(row[8]==(!row[4]&&row[6]==0xd0?0xd0:0),"D0 remains separate reverse propulsion");
        }
        check(in.peek()==std::char_traits<char>::eof(),"command oracle trailing bytes");
        std::puts("5000 original436600/404330 and hooked438540 command records match");
    }
}
