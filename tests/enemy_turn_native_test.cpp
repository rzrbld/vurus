#include "game/enemy_turn_native.h"
#include <iostream>
#include <stdexcept>
using namespace vyruz;
static void need(bool value,const char* why){if(!value)throw std::runtime_error(why);}
int main(){try{
 EnemyTurnConfig cop{0x16,false,true,35,50};
 auto s=initializeEnemyTurn(cop,0,0);
 advanceEnemyBodyTurn(cop,s,1,false,16,0,200);
 need(s.bodyPose==2 && s.logicalHeading==0 && s.bodyRemainingMs==19,"half-heading pose and single-step native countdown");
 advanceEnemyBodyTurn(cop,s,1,false,500,0,200);
 need(s.bodyPose==3 && s.logicalHeading==1 && s.bodyRemainingMs==0,"large tick takes only one pose step");
 advanceEnemyBodyTurn(cop,s,1,false,16,0,200);
 need(s.bodyRemainingMs==19,"aligned controller retains countdown rather than resetting");
 s=initializeEnemyTurn(cop,0,0);s.bodyDesiredPose=25;stepEnemyTurnPose(cop,s,false,false);
 need(s.bodyPose==48,"exact half-ring lower-current tie turns negative");
 s.bodyPose=25;s.bodyDesiredPose=1;stepEnemyTurnPose(cop,s,false,false);
 need(s.bodyPose==26,"exact half-ring higher-current tie turns positive");
 EnemyTurnConfig tank{0x20,true,true,150,50};s=initializeEnemyTurn(tank,0,6);
 advanceEnemyBodyTurn(tank,s,4,true,16,0,200);advanceEnemyGunTurn(tank,s,4,true,16);
 need(s.bodyPose==1 && s.bodyRemainingMs==0 && s.gunPose==6 && s.gunRemainingMs==34,"target-facing gun turns while hull countdown is held");
 s=initializeEnemyTurn(tank,0,6);advanceEnemyBodyTurn(tank,s,1,false,16,0,200);
 need(s.bodyPose==2 && s.gunPose==8 && s.gunPreviousPose==8 && s.gunDesiredPose==7,"body delta carries current/previous gun pose only");
 EnemyTurnConfig rolling{0x1c,false,false,100,0};s=initializeEnemyTurn(rolling,0,0);s.bodyRemainingMs=70;
 advanceEnemyBodyTurn(rolling,s,1,false,16,.75f,100);
 need(s.bodyPose==1 && s.bodyRemainingMs==70,"nonturn-on-place underspeed returns before decrement");
 advanceEnemyBodyTurn(rolling,s,1,false,16,50,100);
 need(s.bodyRemainingMs==34 && s.bodyPeriodMs==50,"speed scales period before native decrement");
 EnemyTurnConfig instant{0x1e,false,true,0,0};s=initializeEnemyTurn(instant,0,0);
 advanceEnemyBodyTurn(instant,s,6,false,500,0,0);
 need(s.bodyPose==2 && !s.bodyRemainingMs,"zero timeout remains single-step");
 s=initializeEnemyTurn(cop,0,0);s.turnCommand=0xcb;
 for(int n=0;n<15;++n){const int stage=advanceEnemyLean(cop,s);need(stage==(n<5?0:n<10?1:2),"Cop three-bank lean thresholds");}
 need(s.lean==-15,"Cop lean saturation");s.turnCommand=0;
 for(int n=0;n<15;++n)advanceEnemyLean(cop,s);need(s.lean==0 && advanceEnemyLean(cop,s)==-1,"neutral relaxes to normal body");
 EnemyTurnConfig burner{0x1a,false,true,18,0};s=initializeEnemyTurn(burner,0,0);s.turnCommand=0xcd;
 need(advanceEnemyLean(burner,s)==0 && s.lean==1,"Burner one-stage signed lean");
 need(enemyNativeTurnCommand(0x16,48,3)==0xcd,"lean command uses numeric order across wrap");
 std::cout<<"native enemy body/gun turn timing, intermediate poses, arc ties, carried gun and lean banks passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
