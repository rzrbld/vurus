#include "render/player_turn.h"
#include <cmath>
#include <fstream>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
using namespace vyruz;
static void need(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
int main(int argc,char**argv){try{unsigned checks=0;
 need(argc==1||argc==2,"usage: player_circular_turn_test [ORIGINAL_TOKEN_RECORDS]");
 struct TokenRecord{uint32_t heading,token,counter,target,outHeading,outToken,outCounter,outTarget;};
 std::ifstream native;if(argc==2){native.open(argv[1],std::ios::binary);need(bool(native),"original token records missing");}
 uint64_t fingerprint=14695981039346656037ull;unsigned tokenChecks=0;
 for(uint32_t h=0;h<24;++h)for(uint32_t token=1;token<=48;++token)for(uint32_t counter=0;counter<=24;++counter){
  PlayerTurnPresentation state;state.reset(h);state.pose=token-1;state.target=16;state.circularCounter=counter;
  const uint32_t nextHeading=(h+(counter?1:0))%24,nextCounter=counter?counter-1:0;
  state.advance(nextHeading,0,nextCounter);
  TokenRecord actual{h,token,counter,17,nextHeading,uint32_t(state.pose+1),nextCounter,uint32_t(state.target+1)};
  if(argc==2){TokenRecord original{};need(bool(native.read(reinterpret_cast<char*>(&original),sizeof original)),"short native token record file");need(std::memcmp(&actual,&original,sizeof actual)==0,"unchanged original444390 tail comparison");}
  const auto* bytes=reinterpret_cast<const unsigned char*>(&actual);for(size_t n=0;n<sizeof actual;++n){fingerprint^=bytes[n];fingerprint*=1099511628211ull;}++tokenChecks;
 }
 need(fingerprint==0x5709ed0078c8b5a5ull,"original token record fingerprint");if(argc==2){char byte;need(!native.read(&byte,1),"extra native token records");}
 for(int hz:{60,90})for(int start=0;start<24;++start){PlayerTurnPresentation p;p.reset(start);p.advance(start,1,0);int heading=start,remaining=0,token=start*2+1;double world=0;
  // Six complete held rings, including every rearm and last-shot counter0.
  for(int frame=0;frame<6*24*hz/60;++frame){world+=60.0/hz;if(world>=1-1e-9){world-=1;if(remaining==0)remaining=24;--remaining;heading=(heading+1)%24;token+=2;if(token>48)token=1;}
   p.advance(heading,1000.0/hz,remaining);need(p.pose==token-1,"forced +2 token must not lag/reverse/restart across held rings");need(p.bank()==25,"forced sweep must not create lean");++checks;
  }
  need(remaining==0&&heading==start,"six complete rings");for(int n=0;n<20;++n)p.advance(start,1000.0/hz,0);need(p.pose==2*start&&p.bank()==25,"release leaves complete ring at native pose");
 }
 // Cooldown-rejected arming is counter24 without an emitted step.
 PlayerTurnPresentation p;p.reset(9);p.advance(9,1,0);p.advance(9,16,24);need(p.pose==18,"armed cooldown does not rotate");p.advance(10,16,23);need(p.pose==20,"pending next emission rotates immediately");
 // A jam/empty-ammo abort clears a pending counter without heading advancement.
 const auto heldPose=p.pose;p.advance(10,16,0);need(p.pose==heldPose,"aborted sweep does not invent remaining shots");
 p.reset(23);p.advance(23,1,0);p.advance(0,18,0);need(p.pose==47,"odd pose before armed abort");p.advance(0,0,24);p.advance(0,16,0);need(p.pose==47,"counter24 abort must not be mistaken for an unobserved full ring");
 // Original unsigned token wrap is >48→1, not modulo preserving parity.
 p.reset(23);p.advance(23,1,0);p.advance(0,18,0);need(p.pose==47,"ordinary wrap half pose");p.advance(1,16,23);need(p.pose==0,"native token48+2 wraps to token1");need(p.target==0,"forced emission retains native ordinary target token");
 // Expiry does not cancel an already pending ring: no active-effect flag enters
 // this contract, only the retained pending counter and emitted logical step.
 p.reset(0);p.advance(0,1,0);p.advance(1,16,23);for(int remaining=22;remaining>=0;--remaining)p.advance((24-remaining)%24,16,remaining);need(p.pose==0,"expired pending ring reaches its final +2 step");
 // A normal mouse/keyboard turn is still the accepted18ms intermediate adapter.
 p.reset(9);p.advance(9,1,0);p.advance(10,18,0);need(p.pose==19&&p.bank()==26,"ordinary half pose and lean preserved");p.advance(10,18,0);need(p.pose==20,"ordinary target completion preserved");
 std::cout<<"PASS "<<checks<<" circular presentation frames +"<<tokenChecks<<" original token records at60/90Hz,6 held rings×24 starts, release, cooldown, abort, token48 wrap, expiry and ordinary turn controls\n";return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
