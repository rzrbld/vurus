#include "game/world.h"
#include "game/save_state.h"
#include "game/save_file.h"
#include "game/shield_regeneration_native.h"
#include "game/properties.h"
#include <bit>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace vyruz;
static void need(bool b,const std::string& why){if(!b)throw std::runtime_error(why);}
static TextNode v(std::string key,std::string text){TextNode n;n.kind=TextKind::Assignment;n.key=key;n.values={text};return n;}
static TextNode block(std::string key,std::vector<TextNode> children,std::string type={}){TextNode n;n.kind=TextKind::Block;n.key=key;n.children=children;if(!type.empty())n.arguments={type};return n;}
static SceneContent fixture(bool authoredBase=false){
 SceneContent s;s.mapPath="TEST/SHIELD.3DG";
 ObjectClass player;player.id={0};player.name="Player";player.behaviours={"Player"};
 player.properties={v("Sub-type","Vyruz"),v("Shield-up-step","0"),block("Energy",{v("Initial","1000"),v("Max","1000")}),block("Shield",{v("Initial","100"),v("Max","1000")})};player.properties.push_back(v("Managed","1"));s.classes.push_back(player);
 for(const std::string family:{"Cop","Walker","Burner","Jeep","Roller","Heli","Turret","Spider","Tank","StaticGen","Thing"}){
  auto c=player;c.id={uint32_t(s.classes.size())};c.name=family;c.behaviours={"Fixed","Seeker"};
  c.properties[0].values={family};c.properties[1].values={"3"};
  c.properties.push_back(block("TYPE",{v("Target","Player"),v("Scan-Delay","1000"),v("Sensor-Range","0")},"Seeker"));
  if(family=="Tank"||family=="Turret"){c.properties.push_back(v("Two-pieces","1"));if(authoredBase)c.properties.push_back(v("Only-base","1"));}
  s.classes.push_back(c);
 }
 for(uint32_t i=0;i<s.classes.size();++i){ObjectPlacement o;o.id={i};o.classId={i};o.position={1000+i*100.f,1000};o.properties=s.classes[i].properties;o.behaviours=s.classes[i].behaviours;s.objects.push_back(o);}s.player=ObjectId{0};
 TextDocument d;std::string error;need(parseContentText("difficulty-level=0\nnmy-shield-up-step-ratio=.5\nplayer-shield-up-step-ratio=2\ndifficulty-level=1\nnmy-shield-up-step-ratio=1\nplayer-shield-up-step-ratio=1\ndifficulty-level=2\nnmy-shield-up-step-ratio=3\nplayer-shield-up-step-ratio=1\ndifficulty-level=3\nnmy-shield-up-step-ratio=4\nplayer-shield-up-step-ratio=1\n","DIFFICULTY.3DC",d,error),error);s.classDocuments={d};return s;
}
static Level arena(){Level l;l.mapW=l.mapH=100;l.hasTerrainCollision=true;return l;}
int main(int argc,char** argv){try{
 // Optional comparison consumes unchanged original-instruction probe records.
 if(argc==2){std::ifstream f(argv[1],std::ios::binary);uint32_t n=0;f.read(reinterpret_cast<char*>(&n),4);size_t checked=0;
  for(uint32_t i=0;i<n;++i){uint32_t row[8];need(bool(f.read(reinterpret_cast<char*>(row),sizeof(row))),"truncated native record");if(row[0])continue;
   const auto x=[](uint32_t bits){return std::bit_cast<float>(bits);};const float result=nativeShieldRegenerationSubtype(row[1])?nativeRegenerateShield(x(row[2]),x(row[3]),x(row[4])):x(row[2]);
   need(std::bit_cast<uint32_t>(result)==row[7],"native shield arithmetic mismatch at "+std::to_string(i));++checked;
  }need(checked==42240,"native shield record count");std::cout<<checked<<" unchanged native regeneration records matched\n";
 }
 need(nativeRegenerateShield(200,0,100)==200,"zero step must not clamp");need(nativeRegenerateShield(1,-3,100)==-2,"no lower clamp");
 const auto level=arena();std::string error;
 for(int difficulty=0;difficulty<4;++difficulty){auto s=fixture();s.objects[0].properties[1].values={"1.25"};s.objects[1].properties[1].values={"5"};GameWorld w;need(w.load(s,level,error,difficulty),error);w.setDebugEnemyAI(false);
  const float ratio[]={.5f,1,3,4};need(w.player().shieldUpStep==1.25f*(difficulty==0?2:1),"player difficulty ratio");
  need(w.actors()[1].shieldUpStep==5*ratio[difficulty],"placement step override and enemy difficulty");
  for(int tick=0;tick<10;++tick)w.tick(tick%2?.014f:.020f,{});
  need(w.player().vitals.shield==100+10*1.25f*(difficulty==0?2:1),"player regen is per pass, independent of elapsed");
  for(size_t i=1;i<w.actors().size();++i)need(w.actors()[i].vitals.shield==100+(i<=9?10*(i==1?5:3)*ratio[difficulty]:0),"family subtype mask/AI-off admission");
  auto snapshot=w.snapshot();snapshot.actors[1].shieldUpStep=-2;snapshot.actors[1].vitals.shield=1;need(w.restoreSnapshot(snapshot,error),error);w.setDebugEnemyAI(false);w.tick(.016f,{});need(w.actors()[1].vitals.shield==-1,"negative runtime step drains below zero");
  auto legacy=w.snapshot();for(auto& a:legacy.actors)a.shieldUpStep.reset();need(w.restoreSnapshot(legacy,error),error);need(w.actors()[1].shieldUpStep==5*ratio[difficulty],"legacy reconstructs saved difficulty once");
  const auto id=w.spawn({1},{3000,1800});need(w.actors()[id].shieldUpStep==3*ratio[difficulty],"generated actors use saved difficulty");
 }
 {auto s=fixture();GameWorld w;need(w.load(s,level,error),error);w.setDebugEnemyAI(false);
  need(w.debugSetSurprise(SurpriseKind::Shield),"debug shield activation");for(int i=0;i<40&&!w.surprise().applied;++i)w.tick(.016f,{});need(w.player().shieldUpStep==10,"Surprise mutates same runtime field");
  const auto start=w.player().vitals.shield;w.tick(.050f,{});need(w.player().vitals.shield==start+10,"one Surprise step per pass, no elapsed producer");
  const auto memory=w.snapshot();SaveGame save;save.currentLevel=s.mapPath;save.levels[s.mapPath].world=memory;std::vector<uint8_t> bytes;SaveGame disk;need(encodeSave(save,bytes,error)&&decodeSave(bytes,disk,error),error);need(bytes[8]==37,"Current writer retains v32 shield state");
  need(disk.levels.at(s.mapPath).world.actors[0].shieldUpStep==0&&!disk.levels.at(s.mapPath).world.surprise.applied,"disk removes temporary +10");need(w.player().shieldUpStep==10,"saving leaves live field unchanged");
  w.cancelSurprise();need(w.player().shieldUpStep==0,"cancel releases +10");need(w.restoreSnapshot(memory,error),error);need(w.player().shieldUpStep==10,"memory rollback keeps +10 without doubling");
  auto expired=memory;expired.surprise.remainingMs=0;need(w.restoreSnapshot(expired,error),error);const float before=w.player().vitals.shield;w.tick(.016f,{});need(w.player().shieldUpStep==0&&w.player().vitals.shield==before,"natural expiry removes producer before pass");
  auto bad=w.snapshot();bad.actors[1].shieldUpStep=INFINITY;need(!w.restoreSnapshot(bad,error),"reject malformed step transactionally");
 }
 {auto s=fixture();GameWorld w;need(w.load(s,level,error),error);w.setDebugEnemyAI(false);need(w.debugSetSurprise(SurpriseKind::TimeWarp),"Time Warp setup");for(int i=0;i<40&&!w.surprise().applied;++i)w.tick(.016f,{});
  const float before=w.actors()[1].vitals.shield;for(int i=0;i<8;++i)w.tick(.016f,{});need(w.actors()[1].vitals.shield==before+6,"world regen follows Time Warp admitted passes");
 }
 {auto s=fixture();s.objects[7].properties.push_back(v("Only-base","1"));s.objects[7].authored.children.push_back(v("Only-base","1"));
  GameWorld w;need(w.load(s,level,error,2),error);need(w.actors()[7].turretOnlyBase&&w.actors()[7].shieldUpStep==3,"fresh placement-only base override retains authored runtime step");
  auto legacy=w.snapshot();legacy.actors[7].shieldUpStep.reset();need(w.restoreSnapshot(legacy,error),error);need(w.actors()[7].shieldUpStep==3,"legacy authored placement base override is distinguishable from converted upper piece");
 }
 for(bool authoredBase:{false,true}){auto s=fixture(authoredBase);GameWorld w;need(w.load(s,level,error,2),error);w.setDebugEnemyAI(false);
  for(size_t id:{size_t(7),size_t(9)}){if(!authoredBase || id==9){w.damage(id,100000);need(w.actors()[id].alive&&w.actors()[id].turretOnlyBase,"first multipart death retains base");need(w.actors()[id].shieldUpStep==0,"converted base explicitly zeros runtime step");}else need(w.actors()[id].shieldUpStep==3,"authored Fixed base has unscaled nonzero step");}
  auto snapshot=w.snapshot();for(auto& a:snapshot.actors)a.shieldUpStep.reset();need(w.restoreSnapshot(snapshot,error),error);need(w.actors()[7].shieldUpStep==(authoredBase?3:0),"legacy distinguishes authored base from converted upper piece");
 }
 std::cout<<"shield per-pass admission, difficulty, overrides, Surprise, Time Warp, destruction and save migration passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
