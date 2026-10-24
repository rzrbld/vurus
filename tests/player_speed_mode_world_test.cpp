#include "game/world.h"
#include "game/save_file.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace vyruz;
void need(bool v,const char* what){if(!v)throw std::runtime_error(what);}
TextNode val(std::string k,std::string v){TextNode n;n.kind=TextKind::Assignment;n.key=k;n.values={v};return n;}
TextNode block(std::string k,std::vector<TextNode> p){TextNode n;n.kind=TextKind::Block;n.key=k;n.children=p;return n;}
struct Fixture{SceneContent scene;Level level;GameWorld world;std::string error;
 Fixture(){scene.mapPath="TEST/SPEED.3DG";scene.player=ObjectId{0};ObjectClass c;c.id={0};c.name="Player";c.behaviours={"Player"};
 c.properties={val("Managed","1"),val("Indestructible","1"),val("Speed-max","45"),val("Turn-timeout","18"),block("Energy",{val("Initial","100"),val("Max","100")}),block("Thrust",{val("Initial","0"),val("Min","-10"),val("Max","45")})};scene.classes={c};ObjectPlacement o;o.id={0};o.classId={0};o.position={2000,2000};o.properties=c.properties;o.behaviours=c.behaviours;scene.objects={o};level.mapW=level.mapH=200;need(world.load(scene,level,error),error.c_str());}
 void ticks(int n){for(int i=0;i<n;++i)world.tick(.014f,{});}
 void apply(SurpriseKind kind){need(world.debugSetSurprise(kind),"debug surprise apply");ticks(40);need(world.surprise().active(kind),"delayed surprise admitted");}
};
std::vector<uint8_t> disk(GameWorld& w){SaveGame s;s.currentLevel="TEST/SPEED.3DG";s.levels[s.currentLevel].world=w.snapshot();std::vector<uint8_t>b;std::string e;need(encodeSave(s,b,e),e.c_str());return b;}
void state(GameWorld& w,float threshold,int period){const auto s=w.snapshot();need(s.nativePlayerSpeed&&s.nativePlayerSpeed->speedThreshold==threshold&&s.nativePlayerSpeed->turnPeriodMs==period&&s.nativePlayerFacing->periodMs==period,"exact live speed/facing state");}
int main()try{
 Fixture f;need(!f.world.snapshot().nativePlayerSpeed,"default state stays unmodified");need(!f.world.playerSpeedIndicatorLow(),"normal panel indicator");
 Fixture relocated;need(relocated.world.togglePlayerSpeedMode(),"low before relocation");
 relocated.world.setPlayerPosition(2100,2200);state(relocated.world,15,27);
 relocated.world.setPlayerDirection(7);state(relocated.world,15,27);
 need(validNativeAwakeSnapshot(relocated.world.snapshot()),"relocation keeps speed/facing snapshot coherent");
 Fixture instance;
 for(auto& property:instance.scene.objects[0].properties){
  if(property.key=="Speed-max")property.values={"60"};
  if(property.key=="Turn-timeout")property.values={"31"};
 }
 need(instance.world.load(instance.scene,instance.level,instance.error),instance.error.c_str());
 const auto instancePeriod=instance.world.snapshot().nativePlayerFacing->periodMs;
 instance.world.restore(instance.world.carry());
 need(instance.world.snapshot().nativePlayerFacing->periodMs==instancePeriod,"untoggled carry preserves placement turning");
 need(instance.world.playerSpeedIndicatorLow(),"HUD compares instance threshold with class default");
 need(instance.world.togglePlayerSpeedMode(),"instance low toggle");state(instance.world,15,int(instancePeriod*1.5));
 need(instance.world.togglePlayerSpeedMode(),"instance high toggle");state(instance.world,45,18);
 Fixture pinball;pinball.apply(SurpriseKind::Pinball);need(!pinball.world.playerSpeedIndicatorLow(),"Pinball elasticity does not select speed panel");
 Fixture slowPanel;slowPanel.apply(SurpriseKind::Slow);need(slowPanel.world.playerSpeedIndicatorLow(),"Slow selects altered-speed panel");
 Fixture xtcPanel;xtcPanel.apply(SurpriseKind::Xtc);need(xtcPanel.world.playerSpeedIndicatorLow(),"XTC selects altered-speed panel");
 WorldInput moving;moving.movement.forward=1;for(int i=0;i<20;++i)f.world.tick(.014f,moving);
 const auto before=f.world.snapshot();f.world.takeAudioEvents();need(f.world.togglePlayerSpeedMode(),"low toggle");state(f.world,15,27);need(f.world.playerSpeedIndicatorLow(),"manual low selects native low-speed panel");
 const auto low=f.world.snapshot();need(low.motion.velocityX==before.motion.velocityX&&low.motion.velocityZ==before.motion.velocityZ&&low.motion.thrust==before.motion.thrust&&low.nativePlayerFacing->remainingMs==before.nativePlayerFacing->remainingMs,"toggle preserves velocity/thrust/countdown");
 auto audio=f.world.takeAudioEvents();need(audio.size()==1&&audio[0].name=="V-LOW-SPEED.WAV"&&audio[0].delayMs==0&&audio[0].holdMs==1000&&audio[0].tailMs==2000,"low native voice tuple");
 need(f.world.togglePlayerSpeedMode(),"high toggle");state(f.world,45,18);need(!f.world.playerSpeedIndicatorLow(),"manual high selects native high-speed panel");audio=f.world.takeAudioEvents();need(audio.size()==1&&audio[0].name=="V-HIGH-SPEED.WAV","high voice");
 need(f.world.togglePlayerSpeedMode(),"low before XTC");f.apply(SurpriseKind::Xtc);state(f.world,30,27);
 auto active=f.world.snapshot();Fixture restored;need(restored.world.restoreSnapshot(active,restored.error),restored.error.c_str());
 need(f.world.togglePlayerSpeedMode()&&restored.world.togglePlayerSpeedMode(),"toggle while XTC");state(f.world,45,18);state(restored.world,45,18);
 need(f.world.debugSetSurprise(SurpriseKind::None)&&restored.world.debugSetSurprise(SurpriseKind::None),"XTC cancellation");state(f.world,15,18);state(restored.world,15,18);
 Fixture xtc;xtc.apply(SurpriseKind::Xtc);need(!xtc.world.snapshot().nativePlayerSpeed,"legacy XTC path remains lazy");need(xtc.world.togglePlayerSpeedMode(),"first toggle during XTC");state(xtc.world,15,27);need(xtc.world.debugSetSurprise(SurpriseKind::None),"cancel lazy XTC");state(xtc.world,45,27);
 f.apply(SurpriseKind::Slow);state(f.world,3.75f,18);f.world.takeAudioEvents();need(!f.world.togglePlayerSpeedMode(),"Slow suppresses manual toggle");need(f.world.takeAudioEvents().empty(),"Slow suppressed toggle silent");need(f.world.debugSetSurprise(SurpriseKind::None),"cancel Slow");state(f.world,15,18);
 // Disk saves normalize a temporary speed effect, retaining the mutable baseline.
 f.apply(SurpriseKind::Xtc);need(f.world.togglePlayerSpeedMode(),"high during disk XTC");state(f.world,45,18);
 auto bytes=disk(f.world);need(bytes[8]==38,"schema38 writer");SaveGame decoded;std::string error;need(decodeSave(bytes,decoded,error),error.c_str());
 auto& snap=decoded.levels.at(decoded.currentLevel).world;need(snap.nativePlayerSpeed&&snap.nativePlayerSpeed->speedThreshold==15&&!snap.nativePlayerSpeed->surpriseBaselineSpeedThreshold,"disk restores pre-XTC threshold");
 need(f.world.debugSetSurprise(SurpriseKind::None),"normalize source XTC");Fixture continuation;need(continuation.world.restoreSnapshot(snap,error),error.c_str());
 for(int i=0;i<100;++i){f.world.tick(.014f,moving);continuation.world.tick(.014f,moving);need(disk(f.world)==disk(continuation.world),"exact speed-mode disk continuation");}
 const auto intact=disk(f.world);auto bad=f.world.snapshot();bad.nativePlayerSpeed->turnPeriodMs=0;need(!f.world.restoreSnapshot(bad,error)&&disk(f.world)==intact,"invalid period transactionally rejected");
 bad=f.world.snapshot();bad.nativePlayerSpeed->speedThreshold=std::numeric_limits<float>::quiet_NaN();need(!f.world.restoreSnapshot(bad,error)&&disk(f.world)==intact,"invalid threshold transactionally rejected");
 bad=f.world.snapshot();bad.nativePlayerSpeed->surpriseBaselineSpeedThreshold=45;need(!f.world.restoreSnapshot(bad,error)&&disk(f.world)==intact,"orphan temporary baseline rejected");
 Fixture carry;carry.world.restore(f.world.carry());state(carry.world,15,18);
 auto cleared=carry.world.carry();cleared.speedMode.reset();carry.world.restore(cleared);
 need(!carry.world.snapshot().nativePlayerSpeed&&carry.world.snapshot().nativePlayerFacing->periodMs==18,"absent carry mode restores authored period");
 Fixture pending;need(pending.world.debugSetSurprise(SurpriseKind::Slow),"pending Slow");
 need(pending.world.togglePlayerSpeedMode(),"pending Slow has no active native d4 exclusion");state(pending.world,15,27);
 pending.ticks(40);state(pending.world,3.75f,27);need(!pending.world.togglePlayerSpeedMode(),"applied Slow exclusion");

 for(const auto kind:{SurpriseKind::Xtc,SurpriseKind::Slow}) {
  Fixture same;need(same.world.togglePlayerSpeedMode(),"carry low mode");same.apply(kind);
  const auto transfer=same.world.carry();same.world.restore(transfer);
  state(same.world,kind==SurpriseKind::Xtc?30.f:3.75f,27);
  need(validNativeAwakeSnapshot(same.world.snapshot()),"same-World carry retains valid temporary baseline");
  need(same.world.debugSetSurprise(SurpriseKind::None),"same-World cancel");state(same.world,15,27);
 }
 auto huge=f.world.snapshot();huge.nativePlayerSpeed->speedThreshold=45;huge.nativePlayerSpeed->turnPeriodMs=1000000;huge.nativePlayerFacing->periodMs=1000000;
 need(f.world.restoreSnapshot(huge,error),error.c_str());f.world.takeAudioEvents();const auto preReject=disk(f.world);
 need(!f.world.togglePlayerSpeedMode()&&f.world.takeAudioEvents().empty()&&disk(f.world)==preReject,"unsavable expanded turn period rejected transactionally");
 std::cout<<"PASS live native speed mode, XTC/Slow, voices, schema38, disk continuation and invalid-state rollback\n";return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}
