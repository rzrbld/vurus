#include "snapshot_fixture.h"
#include "game/world.h"
#include "game/save_state.h"
#include "game/save_file.h"
#include <cstdlib>
#include <iostream>
using namespace vyruz;
void check(bool ok,const char* why){if(!ok){std::cerr<<why<<'\n';std::exit(1);}}
TextNode prop(std::string k,std::string v){TextNode n;n.kind=TextKind::Assignment;n.key=k;n.values={v};return n;}
TextNode type(std::string name,std::vector<TextNode> nodes){TextNode n;n.kind=TextKind::Block;n.key="TYPE";n.arguments={name};n.children=nodes;return n;}
SceneContent fixture(bool timeout=true,float reactivate=10000,bool random=false){
 SceneContent s;s.mapPath="TEST/A.3DG";ObjectClass p;p.id={0};p.name="Player";p.behaviours={"Player"};
 p.properties={prop("Bounding-box-size","10"),prop("Managed","1")};s.classes.push_back(p);
 ObjectClass c;c.id={1};c.name="Code";c.properties={prop("Carry","1"),prop("Bounding-box-size","10"),type("Item",{prop("Only-for-player","1"),prop("Has-timeout",timeout?"1":"0"),prop("Timeout","30000"),prop("Reactivate-after",std::to_string(reactivate)),prop("Random-Location",random?"1":"0"),prop("Warn-timeout","5000"),prop("Warn-message-format","Left %d!")})};s.classes.push_back(c);
 for(int i=0;i<3;++i){ObjectPlacement o;o.id={unsigned(i)};o.classId={unsigned(i?1:0)};o.properties=s.classes[o.classId.value].properties;o.behaviours=s.classes[o.classId.value].behaviours;o.position={float(200+i*100),200};if(i)o.properties.push_back(type("Key",{prop("Id","10"),prop("Is-code","1"),prop("Code-part-id",std::to_string(i)),prop("Code-part-count","2")}));s.objects.push_back(o);}s.player=ObjectId{0};return s;
}
void advance(GameWorld& w,int ticks){for(int i=0;i<ticks;++i)w.tick(.1f,{});}
int main(){Level level;level.mapW=20;level.mapH=20;level.hasTerrainCollision=true;level.collisionMask.assign(400,0);level.collisionMask[1]=0xff;level.collisionMask[2]=0x1ff;std::string error;
 for(int mode=0;mode<4;++mode){auto s=fixture(mode!=2,mode==1?0:10000,mode==3);GameWorld w;check(w.load(s,level,error),"load");w.setPlayerPosition(300,200);w.movePlayer(0,0);check(w.player().heldKeys.size()==1,"part pickup");w.setPlayerPosition(500,400);advance(w,310);check(!w.actors()[1].alive&&w.player().heldKeys[0].held->remainingMs==30000,"incomplete no timer or clone");w.setPlayerPosition(400,200);w.movePlayer(0,0);w.setPlayerPosition(500,400);advance(w,41);check(w.player().heldKeys[0].held->remainingMs==25900,"complete timer");bool warning=false;for(auto& n:w.notifications().entries())warning|=n.text=="Left 25!";check(warning,"25 second warning");
 SaveGame save;save.currentLevel=s.mapPath;save.levels[s.mapPath].world=w.snapshot();std::vector<uint8_t> bytes;SaveGame decoded;check(encodeSave(save,bytes,error)&&decodeSave(bytes,decoded,error),"disk timer");check(w.restoreSnapshot(decoded.levels.at(s.mapPath).world,error),"restore timer");const auto beforeExpiry=w.captureNativePool();advance(w,259);
 if(mode==2){check(w.player().heldKeys.size()==2&&w.player().heldKeys[0].held->remainingMs==0,"non-timed retains zero");continue;}
 check(w.player().heldKeys.empty(),"expiry removes inventory");check(w.snapshot().heldItems.empty(),"Code expiry releases generic retained-item bookkeeping");check(validNativeWorldPoolSnapshot(w.snapshot()),"Code expiry pool ownership remains valid");check(w.captureNativePool().pool.nextGeneration==beforeExpiry.pool.nextGeneration,"Code expiry never allocates a replacement UID");check(w.captureNativePool().pool.liveCount==beforeExpiry.pool.liveCount-(mode==1?2:0),"Code destroy releases two bodies; return retains both bodies");check(w.actors()[1].alive==(mode!=1)&&w.actors()[2].alive==(mode!=1),"return or destroy all parts");if(mode!=1){check(w.actors()[1].reactivateMs==-1&&w.actors()[1].itemTimeoutMs==30000,"immediate active class reset");if(mode==3)check(s.objects[1].position.x==96&&s.objects[1].position.y==16&&s.objects[2].position.x==96,"random exact mask");}
 }
 // Placement first timeout differs from class timeout, and carries cannot bind another map's same index.
 auto s=fixture();s.objects[1].properties[2].children[2]=prop("Timeout","100");GameWorld w;check(w.load(s,level,error),"override load");w.setPlayerPosition(300,200);w.movePlayer(0,0);w.setPlayerPosition(400,200);w.movePlayer(0,0);auto carry=w.carry();auto other=fixture();other.mapPath="TEST/B.3DG";GameWorld cross;check(cross.load(other,level,error),"other load");cross.restore(carry);cross.setPlayerPosition(800,400);const auto crossPool=cross.captureNativePool();cross.tick(.1f,{});check(cross.captureNativePool().pool.nextGeneration==crossPool.pool.nextGeneration&&cross.captureNativePool().pool.liveCount==crossPool.pool.liveCount&&validNativeWorldPoolSnapshot(cross.snapshot()),"cross-level Code return preserves both original pool identities");check(other.objects.size()==5&&cross.actors()[1].alive&&cross.actors()[3].itemTimeoutMs==30000,"cross-level appends carried identity with class reset");
 // Legacy recovery is unique and stops the old generic respawn timer.
 auto legacy=w.snapshot();for(auto& k:legacy.actors[0].heldKeys)k.held.reset();legacy.actors[1].reactivateMs=10;vyruz_test::synchronizeActor(legacy,1);check(w.restoreSnapshot(legacy,error),"legacy restore");check(w.player().heldKeys[0].held&&w.actors()[1].reactivateMs==-1,"legacy identity migration");
 // Ordinary same-ID keys survive expiry; duplicate first parts tick the oldest timer twice.
 auto duplicateScene=fixture();GameWorld duplicates;check(duplicates.load(duplicateScene,level,error),"duplicate load");duplicates.setPlayerPosition(300,200);duplicates.movePlayer(0,0);auto state=duplicates.snapshot();auto first=state.actors[0].heldKeys[0];
 // A second actual part1 placement represents the native duplicate case.
 auto second=state.objects[1];second.id={2};second.position=state.objects[2].position;state.objects[2]=second;
 first.held->object=second;state.actors[2].alive=false;state.actors[0].heldKeys.push_back(first);state.actors[0].heldKeys.push_back({10,false,1,1,"ordinary"});state.actors[0].heldKeys[0].held->remainingMs=300;vyruz_test::synchronizeActor(state,2);check(duplicates.restoreSnapshot(state,error),"duplicate restore");auto invalid=state;invalid.actors[0].heldKeys[0].held->object.properties.back().children[0]=prop("Id","99");
 check(!duplicates.restoreSnapshot(invalid,error)&&duplicates.player().heldKeys[0].held->remainingMs==300,"mismatched held identity rejected transactionally");
 duplicates.setPlayerPosition(800,400);duplicates.tick(.1f,{});check(duplicates.player().heldKeys[0].held->remainingMs==100 && duplicates.player().heldKeys[1].held->remainingMs==30000,"oldest part1 receives both native scans");duplicates.tick(.1f,{});check(duplicates.player().heldKeys.size()==1 && !duplicates.player().heldKeys[0].code,"ordinary same-ID key survives code group expiry");
 std::cout<<"timed code fixtures passed\n";
}
