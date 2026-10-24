#include "game/enemy_weapon_readiness.h"
#include "game/ordinary_enemy_ai_native.h"
#include <cstdlib>
#include <iostream>
using namespace vyruz;
void need(bool b,const char* m){if(!b){std::cerr<<m<<'\n';std::exit(1);}}
int main(){
 // Original42d2d0 execution fingerprint; all fixtures are in range, facing
 // their terminal target. Timestamp and active-Ammo words are native uint32.
 const uint32_t types[]={0x16,0x17,0x19,0x1a,0x1b,0x1c,0x1d,0x1e,0x20,0x1f};
 const uint32_t times[]={0,1,14,42,149,150,151,2000,0xfffffff0u,0xffffffffu};
 const uint32_t lasts[]={0,42,150,0xfffffff0u,0xffffffffu},delays[]={0,1,42,150,2000,0xffffffffu};
 const uint32_t counts[]={0,1,0x7fffffffu,0x80000000u,0xffffffffu};
 uint64_t hash=14695981039346656037ULL;unsigned cases=0;
 const auto word=[&](uint32_t v){for(unsigned b=0;b<4;++b){hash^=uint8_t(v>>(8*b));hash*=1099511628211ULL;}};
 for(auto type:types)for(auto now:times)for(auto last:lasts)for(auto delay:delays)for(bool one:{false,true})for(auto count:counts){
  WeaponConfig config;config.delayMs=delay;config.oneByOne=one;WeaponState state;state.lastFireMs=last;
  NativeEnemyWeaponAimInput in;in.source={1500,500};in.target={1600,500};in.bodyHeading=in.gunHeading=in.desiredHeading=6;
  in.subtype=int(type);in.bodyPose=7;in.range=300;in.status=4;in.movementCommand=208;
  in.targetPresent=in.terminal=in.canTurnOnPlace=in.weaponPresent=true;
  in.ready=nativeEnemyWeaponReady(config,state,now,count);const auto out=nativeEnemyWeaponAim(in);
  for(auto v:{type,now,last,delay,uint32_t(one),count,uint32_t(out.fire),uint32_t(out.admitted),uint32_t(out.movementCommand)})word(v);
  ++cases;
 }
 need(cases==30000&&hash==0x297601c70f3ec8bdULL,"Original enemy readiness/aim timer fingerprint");
 WeaponConfig config;config.delayMs=150;WeaponState state;
 need(!nativeEnemyWeaponReady(config,state,149,0)&&nativeEnemyWeaponReady(config,state,150,0),"Absent timestamp means native zero, not immediate enemy admission");
 need(nativeEnemyWeaponReady(config,state,1000,0),"Late births use absolute World clock, not actor age");
 config.oneByOne=true;need(nativeEnemyWeaponReady(config,state,0,0),"One-by-one raw zero bypasses initial delay");
 need(!nativeEnemyWeaponReady(config,state,0,0xffffffffu),"Signed-negative raw counter is nonzero");
 std::cout<<"PASS30000 original enemy timer/aim cases and default timestamp controls\n";
}
