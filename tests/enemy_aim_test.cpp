#include "game/enemy_aim.h"
#include "game/combat.h"
#include <iostream>
#include <stdexcept>
using namespace vyruz;
static void need(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
int main(){try{
 const ContentPosition source{4000,3000};
 for(int target=0;target<24;++target){const auto offset=projectileStartOffset(target,1000);
  const ContentPosition position{source.x+offset.x,source.y+offset.y};
  for(int facing=0;facing<24;++facing){
   const auto ordinary=enemyFireAim(source,position,target,facing,250,350,true);
   need(ordinary.inRange==(target==facing)&&ordinary.fire==(target==facing),"native exact heading gate");
   const auto guided=enemyFireAim(source,position,(target+3)%24,facing,250,350,false,true);
   need(guided.inRange&&guided.fire==(target==facing),"Tracker ammo route/desired exception retains actual heading gate");
  }
  need(!enemyFireAim(source,position,target,target,50,350,true).inRange,"Min-distance is attack admission");
  need(!enemyFireAim(source,position,target,target,250,50,true).inRange,"weapon range remains additional gate");
  need(!enemyFireAim(source,position,target,target,250,350,false).fire,"ordinary blocked route rejects shot");
  const auto waiting=enemyFireAim(source,position,(target+1)%24,(target+1)%24,250,350,true);
  need(waiting.inRange&&!waiting.fire,"in-range spin/reveal independent of actual shot");
 }
 need(!enemyFireAim(source,source,0,0,250,350,true).fire,"coincident heading sentinel rejects actual fire");
 // Golden Left offsets from unchanged native443d0a..443d4a, gap25.
 static constexpr ShotPositionOffset originalLeft[]={
  {11.180339813232422f,5.590169906616211f},
  {13.693063735961914f,3.9528470039367676f},
  {15.272627830505371f,2.0461440086364746f},
  {15.81138801574707f,-8.740229873183125e-08f},
  {15.272627830505371f,-2.046144485473633f},
  {13.693063735961914f,-3.9528472423553467f},
  {11.180338859558105f,-5.590169906616211f},
  {7.905693054199219f,-6.846532344818115f},
  {4.092287540435791f,-7.6363139152526855f},
  {-1.3693254459212767e-06f,-7.905694007873535f},
  {-4.092289924621582f,-7.6363139152526855f},
  {-7.905694961547852f,-6.846531391143799f},
  {-11.180340766906738f,-5.590169429779053f},
  {-13.69306468963623f,-3.9528465270996094f},
  {-15.272628784179688f,-2.0461435317993164f},
  {-15.81138801574707f,8.59467263580882e-07f},
  {-15.272627830505371f,2.04614520072937f},
  {-13.693062782287598f,3.952847957611084f},
  {-11.180337905883789f,5.590170383453369f},
  {-7.905692100524902f,6.846532344818115f},
  {-4.092286109924316f,7.636314392089844f},
  {2.0604632027243497e-06f,7.905694007873535f},
  {4.09229040145874f,7.636313438415527f},
  {7.905696868896484f,6.846531391143799f},
 };
 for(int h=0;h<24;++h){const auto left=projectileBarrelOffset(h,12.5f),right=projectileBarrelOffset(h,-12.5f);
  need(std::abs(left.x-originalLeft[h].x)<.000002f&&std::abs(left.y-originalLeft[h].y)<.000002f,"native all-heading barrel golden values");
  need(right.x==-left.x&&right.y==-left.y,"native opposite barrel signs");
 }
 const auto left=projectileBarrelOffset(0,12.5f),right=projectileBarrelOffset(0,-12.5f);
 need(std::abs(left.x-11.18034f)<.00001f&&std::abs(left.y-5.59017f)<.00001f,"original left barrel side");
 need(right.x==-left.x&&right.y==-left.y,"original right barrel side");
 WeaponConfig config;config.initialSlope=-.75f;config.slopeStep=.01f;config.delayMs=100;
 WeaponState state;uint32_t ammo=10;
 for(int tick=0;tick<20;++tick)need(fireWeapon(config,state,false,tick,ammo,true).empty(),"tracking cannot consume ammunition");
 need(ammo==10&&state.slopeHeld&&state.slopeRadians>config.initialSlope,"tracking advances slope while no shot admitted");
 need(fireWeapon(config,state,true,20,ammo,true).size()==1&&ammo==9,"aligned shot uses retained slope");
 fireWeapon(config,state,false,21,ammo,false);need(!state.slopeHeld&&state.slopeRadians==config.initialSlope,"target loss resets slope");
 std::cout<<"enemy native heading/range/route admission, barrel sides and separate spin-up passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
