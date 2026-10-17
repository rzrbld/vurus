#include "game/death_drops.h"
#include "game/event_random_native.h"
#include "game/world.h"
#include "game/save_file.h"
#include <cstdlib>
#include <iostream>
#include <sstream>
using namespace vyruz;
static void check(bool ok,const char* why){if(!ok){std::cerr<<why<<'\n';std::exit(1);}}
static TextNode directive(std::string key,std::vector<std::string> args){TextNode n;n.kind=TextKind::Bare;n.key=key;n.arguments=args;return n;}
static SceneContent fixture(){
 SceneContent s;s.mapPath="TEST/DROPS.3DG";
 for(const auto name:{"Player","Box","A","B","C"}){ObjectClass c;c.id={static_cast<uint32_t>(s.classes.size())};c.name=name;c.behaviours={c.id.value==0?"Player":"Fixed"};s.classes.push_back(c);}
 s.classes[1].properties={directive("RANDOM",{"A","1","1"}),directive("HAVE",{"B"}),directive("RANDOM",{"C","1","1"})};
 for(unsigned i=0;i<4;++i){ObjectPlacement o;o.id={i};o.classId={i?1u:0u};o.position={i?500.75f:100.f,i?300.5f:100.f};o.behaviours=s.classes[o.classId.value].behaviours;o.properties=s.classes[o.classId.value].properties;s.objects.push_back(o);}s.player=ObjectId{0};return s;
}
int main(){
 check(deathDropInteger("-4")==4 && deathDropInteger("+4")==4 && deathDropInteger("0")==0,"native absolute integer parameters");
 check(!deathDropInteger("1x")&&!deathDropInteger("2147483648"),"invalid drop parameters rejected");
 check(deathDropRoll(0,4)==1 && deathDropRoll(0x3fffffffu,4)==1 && deathDropRoll(0x40000000u,4)==2 && deathDropRoll(UINT32_MAX,4)==4,"native inclusive range boundaries");
 check(deathDropRoll(UINT32_MAX,0)==1 && deathDropRoll(UINT32_MAX,1)==1,"native degenerate range");
 DeathDropScatter scatter;const ContentPosition offsets[]={{0,0},{-32,0},{32,0},{0,-10},{-42,-10},{42,-10},{0,10},{-52,10},{52,10},{-62,0}};
 for(auto p:offsets){auto actual=scatter.next({500.75f,300.5f});check(actual.x==500+p.x&&actual.y==300+p.y,"native alternating scatter and truncation");}
 Level level;level.mapW=100;level.mapH=100;level.hasTerrainCollision=true;auto s=fixture();GameWorld world;std::string error;check(world.load(s,level,error),"load");
 const auto before=world.snapshot();world.damage(1,100000,0);
 check(s.objects.size()==7,"all independent successful drops created");
 check(s.objects[4].classId.value==3 && s.objects[5].classId.value==4 && s.objects[6].classId.value==2,"HAVE first then reverse RANDOM");
 check(s.objects[4].position.x==500 && s.objects[5].position.x==468 && s.objects[6].position.x==532,"scatter shared across HAVE and RANDOM");
 auto expected=before.nativeGlobalRandom;nativeDeathRandomDrop(expected,1,1);nativeDeathRandomDrop(expected,1,1);
 const auto after=world.snapshot();check(after.nativeGlobalRandom.seed==expected.seed&&after.nativeGlobalRandom.counter==expected.counter&&after.randomState==before.randomState,"certain RANDOM consumes native global rolls; HAVE consumes none and MT stream is unchanged");
 world.damage(1,100000,0);check(s.objects.size()==7,"destroyed actor drops only once");
 // A checkpoint restores the sequence and positions of later drops, including
 // failed rolls, rather than reseeding or consuming random numbers on load.
 SaveGame save;save.currentLevel=s.mapPath;save.levels[s.mapPath].world=world.snapshot();std::vector<uint8_t> bytes;SaveGame decoded;
 check(encodeSave(save,bytes,error)&&decodeSave(bytes,decoded,error),"drop checkpoint encoding");
 auto clone=fixture();GameWorld resumed;check(resumed.load(clone,level,error)&&resumed.restoreSnapshot(decoded.levels.at(s.mapPath).world,error),"drop checkpoint restoration");
 for(size_t i:{2u,3u}){world.damage(i,100000,0);resumed.damage(i,100000,0);}
 check(s.objects.size()==clone.objects.size()&&world.snapshot().nativeGlobalRandom.seed==resumed.snapshot().nativeGlobalRandom.seed&&world.snapshot().nativeGlobalRandom.counter==resumed.snapshot().nativeGlobalRandom.counter,"continued drop count and RNG");
 for(size_t i=4;i<s.objects.size();++i)check(s.objects[i].classId==clone.objects[i].classId&&s.objects[i].position.x==clone.objects[i].position.x&&s.objects[i].position.y==clone.objects[i].position.y,"continued drop identities and positions");
 // Guaranteed failures consume rolls, but no scatter slots. A later HAVE
 // still appears at the origin even when its directive follows RANDOM.
 auto failed=fixture();failed.objects[1].properties={directive("HAVE",{"A"}),directive("RANDOM",{"B","0","4"}),directive("RANDOM",{"C","0","4"})};
 GameWorld f;check(f.load(failed,level,error),"failed-roll load");f.damage(1,100000,0);check(failed.objects.size()==5&&failed.objects[4].classId.value==2&&failed.objects[4].position.x==500,"failed rolls add no objects or offsets");
 auto overridden=fixture();overridden.objects[1].properties={directive("HAVE",{"A"}),directive("Override-Objects",{}),directive("HAVE",{"B"}),directive("RANDOM",{"C","1","1"})};
 GameWorld ov;check(ov.load(overridden,level,error),"override load");ov.damage(1,100000,0);
 check(overridden.objects.size()==6 && overridden.objects[4].classId.value==3 && overridden.objects[5].classId.value==4,"Override-Objects suppresses inherited HAVE, retains later HAVE and RANDOM");
 std::cout<<"death drop fixtures passed\n";
}
