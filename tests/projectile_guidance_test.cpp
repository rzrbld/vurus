#include "game/projectile_guidance.h"
#include "game/controls.h"
#include <iostream>
#include <stdexcept>
#include <vector>
using namespace vyruz;
static void need(bool b,const char*m){if(!b)throw std::runtime_error(m);}
int main(){try{
 GuidanceLaunchQuery q;q.position={2000,1000};q.cellX=30;q.cellY=30;q.mapWidth=q.mapHeight=100;q.playerShooter=true;
 auto candidate=[&](size_t id,int x,int y,int heading=0,float distance=100){auto v=nativeHeading(heading);auto p=nativeToSurface(v.x*distance,v.y*distance);return GuidanceLaunchCandidate{id,ClassId{7},{q.position.x+p.x,q.position.y+p.y},x,y,true,true,true};};
 std::vector<GuidanceLaunchCandidate> c={candidate(1,32,30,0,1),candidate(2,31,30,0,1000)};
 need(selectGuidanceLaunchTarget(q,c)==2,"ring precedes Euclidean distance");
 c={candidate(1,31,31),candidate(2,31,29),candidate(3,29,29)};need(selectGuidanceLaunchTarget(q,c)==3,"top row then left cell within ring");
 c={candidate(9,30,30),candidate(1,30,30)};need(selectGuidanceLaunchTarget(q,c)==9,"same-cell linked/span order not actor ID");
 for(int heading:{0,2,22,3,21,12}){c={candidate(1,30,30,heading)};need(bool(selectGuidanceLaunchTarget(q,c))==(heading==0||heading==2||heading==22),"strict three-heading cone including wrap");}
 c={candidate(1,49,30),candidate(2,50,30)};need(selectGuidanceLaunchTarget(q,c)==1,"twenty rings includes19 excludes20");c.erase(c.begin());need(!selectGuidanceLaunchTarget(q,c),"outside ring limit");
 c={candidate(1,-1,30),candidate(2,30,30)};c[1].eligible=false;need(!selectGuidanceLaunchTarget(q,c),"grid bounds/membership");
 c={candidate(1,30,30)};c[0].countsAsEnemy=false;need(!selectGuidanceLaunchTarget(q,c),"player enemy classifier");q.playerShooter=false;need(!selectGuidanceLaunchTarget(q,c),"nonplayer requires target class");q.targetClass=ClassId{7};need(selectGuidanceLaunchTarget(q,c)==1,"nonplayer matches class independently of hostility");q.targetClass=ClassId{8};need(!selectGuidanceLaunchTarget(q,c),"class mismatch");q.targetClass=ClassId{7};c[0].ordinaryActor=false;need(!selectGuidanceLaunchTarget(q,c),"nonplayer excludes special actors");
 c[0].ordinaryActor=true;c[0].position=q.position;need(selectGuidanceLaunchTarget(q,c)==1,"native invalid-heading coincident candidate");q.shooterEligible=false;need(!selectGuidanceLaunchTarget(q,c),"special shooter exclusion");
 GuidanceTrackingQuery t;t.position=q.position;t.cellX=t.cellY=30;t.mapWidth=t.mapHeight=100;t.sensorRange=600;t.visionDegrees=60;t.targetClass=ClassId{7};
 c={candidate(1,31,29),candidate(2,29,31)};need(selectGuidanceTrackingTarget(t,c)==2,"rescan x-major order differs from launch ring order");
 c={candidate(1,30,30)};c[0].visible=false;need(!selectGuidanceTrackingTarget(t,c),"rescan rejects invisible");c[0].visible=true;c[0].activationDelayMs=1;need(!selectGuidanceTrackingTarget(t,c),"rescan rejects pending activation");c[0].activationDelayMs=0;c[0].ordinaryActor=false;need(selectGuidanceTrackingTarget(t,c)==1,"rescan has no launch special-subtype exclusion");c[0].classedActor=false;need(!selectGuidanceTrackingTarget(t,c),"rescan requires classed actor");
 c={candidate(1,46,30)};need(selectGuidanceTrackingTarget(t,c)==1,"projected diameter includes16tileoffset at range600");c[0].cellX=47;need(!selectGuidanceTrackingTarget(t,c),"projected diameter excludes17tileoffset");
 c={candidate(1,30,30,3)};need(!selectGuidanceTrackingTarget(t,c),"authored60degrees gives two discrete headings");t.visionDegrees=90;need(selectGuidanceTrackingTarget(t,c)==1,"authored90degrees gives three headings");
 c={candidate(1,30,30,0,7000)};need(!selectGuidanceTrackingTarget(t,c),"rescan native radius excludes distant candidate inside cell rectangle");
 // Native407630 ->4075c0 rounds this boundary to4; the former float
 // atan2 adapter rounded it to5 and changed whether launch acquired a lock.
 {
  GuidanceLaunchQuery boundary;boundary.mapWidth=boundary.mapHeight=100;
  boundary.playerShooter=true;boundary.heading=2;
  GuidanceLaunchCandidate target;target.id=17;target.countsAsEnemy=true;
  target.position={242.030228f,292.156372f};
  std::vector<GuidanceLaunchCandidate> targets{target};
  need(selectGuidanceLaunchTarget(boundary,targets)==17,"native boundary heading4 admits shooter heading2");
  boundary.heading=7;
  need(!selectGuidanceLaunchTarget(boundary,targets),"native boundary heading4 rejects shooter heading7");
 }
 //42da80 reads stored native centers, independently from surface sprite
 // positions. Both radius and cone must follow those centers.
 {
  GuidanceTrackingQuery centers;centers.mapWidth=centers.mapHeight=100;
  centers.sensorRange=600;centers.visionDegrees=60;centers.targetClass=ClassId{7};
  centers.nativeCenter=ContentPosition{100,200};
  GuidanceLaunchCandidate target;target.id=23;target.classId=ClassId{7};
  target.position={50000,-50000};target.nativeCenter=ContentPosition{100,800};
  std::vector<GuidanceLaunchCandidate> targets{target};
  need(selectGuidanceTrackingTarget(centers,targets)==23,"stored native centers admit exact600 radius despite distant surface origin");
  targets[0].nativeCenter=ContentPosition{100,800.001f};
  need(!selectGuidanceTrackingTarget(centers,targets),"stored native centers reject beyond600 radius");
  targets[0].nativeCenter=ContentPosition{600,200};
  need(!selectGuidanceTrackingTarget(centers,targets),"stored native centers control heading cone");
  centers.nativeCenter.reset();targets[0].nativeCenter=ContentPosition{0,500};
  need(selectGuidanceTrackingTarget(centers,targets)==23,"query surface fallback preserves candidate native override");
  centers.nativeCenter=ContentPosition{0,-500};targets[0].nativeCenter.reset();targets[0].position={0,0};
  need(selectGuidanceTrackingTarget(centers,targets)==23,"candidate surface fallback preserves query native override");
  centers.nativeCenter=ContentPosition{0,-1000};
  need(!selectGuidanceTrackingTarget(centers,targets),"query native override controls fallback candidate range");
 }
 std::cout<<"PASS native launch ring order, exact heading boundaries, native-center rescan, eligibility and bounds\n";return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
