#include "game/world.h"
#include "game/menu.h"
#include "game/save_file.h"
#include "io/vfs.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace vyruz;
void need(bool b,const std::string& e){if(!b)throw std::runtime_error(e);}
std::vector<uint8_t> bytes(const GameWorld& w,const std::string& path){SaveGame save;save.currentLevel=path;save.levels[path].world=w.snapshot();std::vector<uint8_t> out;std::string e;need(encodeSave(save,out,e),e);return out;}
int main(int argc,char**argv)try{
 need(argc==3||argc==4,"resource_world DATA OUTPUT");Vfs v;need(v.init(argv[1]),v.error());ContentReader read=[&](const auto&p,auto&b){return v.read(p,b);};std::filesystem::create_directories(argv[2]);unsigned maps=0,graphs=0,frames=0,knownBodies=0,absentBodies=0;
 for(const auto& path:debugLevels()){if(argc==4&&path!=argv[3])continue;
  SceneContent original;Level level;std::string e;need(loadSceneContent(read,path,original,e),e);std::vector<uint8_t> d,i,c;const auto prefix=path.substr(0,path.rfind('.'));need(v.read(path,d)&&loadLevel(d,level)&&v.read(prefix+".3DI",i)&&v.read(prefix+".3DX",c)&&loadTerrain(i,c,level),"Terrain "+path);
  auto a=original,b=original;GameWorld live,resumed;live.setEffectReader(read);resumed.setEffectReader(read);need(live.load(a,level,e),e);live.setDebugGodMode(true);live.debugFillAmmo();
  for(size_t id=0;id<live.actors().size();++id)if(const auto& actor=live.actors()[id];actor.nativeSubtype==0x10 || actor.nativeSubtype==0x15) {
   need(bool(actor.bodyGraphics),"Body history missing "+path);
   need(actor.bodyGraphics->binding!=NativeBodyGraphicsBinding::Unknown,"Original body metadata Unknown "+path+" id"+std::to_string(id)+" "+a.objects[id].sprite.path);
   if(actor.bodyGraphics->binding==NativeBodyGraphicsBinding::Present)++knownBodies;else ++absentBodies;
  }
  if(path=="CAVE/T1C2.3DG")live.setPlayerPosition(5230,1080);
  if(path=="UNDERGROUND/T2U1.3DG")live.setPlayerPosition(4870,2180);
  if(path=="HITEK/T1H1.3DG")live.setPlayerPosition(3670,2570);
  if(path=="HITEK/T2H2.3DG")live.setPlayerPosition(4571,2841);
  const auto phases=[](GameWorld& w){auto p=w.playerPosition();BodyGraphicsView view{static_cast<int32_t>(320-p.x),static_cast<int32_t>(240-p.y),640,480};w.advanceBodyGraphicsPhase(view,BodyGraphicsPhase::VisibleUpdate);w.advanceBodyGraphicsPhase(view,BodyGraphicsPhase::Draw);w.takeAudioEvents();w.discardBodyGraphicsAudio();};
  const auto input=[](unsigned tick){WorldInput in;in.fire=tick%80<40;in.movement.forward=(tick/40)%2?1:0;return in;};
  for(unsigned t=0;t<120;++t){live.setPlayerDirection((t/30)%24);live.tick(1.f/60,input(t));phases(live);}
  auto state=live.snapshot();if(state.spiderNavigation){++graphs;need(state.spiderNavigation->xEdges.usesNativeInsertion()&&state.spiderNavigation->yEdges.usesNativeInsertion(),"Fresh World graph policy");}
  SaveGame save,decoded;save.currentLevel=path;save.levels[path].world=state;auto file=std::filesystem::path(argv[2])/("map"+std::to_string(maps)+".vzs");need(writeSaveFile(file.string(),save,e)&&readSaveFile(file.string(),decoded,e),e);need(resumed.load(b,level,e)&&resumed.restoreSnapshot(decoded.levels.at(path).world,e),e);resumed.setDebugGodMode(true);
  need(bytes(live,path)==bytes(resumed,path),"Initial disk state "+path);
  for(auto* w:{&live,&resumed}){auto p=w->playerPosition();w->advanceBodyGraphicsPhase({static_cast<int32_t>(320-p.x),static_cast<int32_t>(240-p.y),640,480},BodyGraphicsPhase::Draw);w->takeAudioEvents();w->discardBodyGraphicsAudio();}
  need(bytes(live,path)==bytes(resumed,path),"Immediate postload draw "+path);
  for(unsigned t=120;t<180;++t){for(auto* w:{&live,&resumed}){w->setPlayerDirection((t/30)%24);w->tick(1.f/60,input(t));phases(*w);}auto lb=bytes(live,path),rb=bytes(resumed,path);if(lb!=rb){
   std::ofstream lf(std::filesystem::path(argv[2])/"live.raw",std::ios::binary),rf(std::filesystem::path(argv[2])/"resumed.raw",std::ios::binary);lf.write((const char*)lb.data(),lb.size());rf.write((const char*)rb.data(),rb.size());
   size_t first=28;while(first<lb.size()&&first<rb.size()&&lb[first]==rb[first])++first;std::cerr<<"first difference="<<first<<" sizes="<<lb.size()<<'/'<<rb.size()<<'\n';
   auto ls=live.snapshot(),rs=resumed.snapshot();for(const auto* ss:{&ls,&rs}){std::cerr<<"actors="<<ss->actors.size()<<" shots="<<ss->projectiles.size()<<'\n';if(ss->spiderNavigation){const auto& g=*ss->spiderNavigation;std::cerr<<"graph objects="<<g.objects.size();for(const auto* ax:{&g.xEdges,&g.yEdges}){auto st=ax->insertionState();std::cerr<<" axis="<<ax->size()<<" pivot="<<(st.pivot?st.pivot->object:UINT32_MAX)<<"/"<<(st.pivot&&st.pivot->maximum)<<" balance="<<st.balance;}std::cerr<<'\n';}}
   if(ls.spiderNavigation&&rs.spiderNavigation){const auto& lg=*ls.spiderNavigation;const auto& rg=*rs.spiderNavigation;for(bool xx:{true,false}){const auto& la=xx?lg.xEdges:lg.yEdges;const auto& ra=xx?rg.xEdges:rg.yEdges;auto it=ra.begin();unsigned rank=0;for(auto e:la){if(it==ra.end())break;if(e.object!=it->object||e.maximum!=it->maximum)std::cerr<<"edge diff "<<xx<<" rank="<<rank<<" live="<<e.object<<'/'<<e.maximum<<" restored="<<it->object<<'/'<<it->maximum<<'\n';++it;++rank;}}}
   need(false,"Disk continuation "+path+" tick"+std::to_string(t));}++frames;}
  std::cout<<path<<",actors="<<live.actors().size()<<",graph="<<bool(state.spiderNavigation)<<",disk_ticks=60\n";++maps;
 }
 need(maps==(argc==4?1u:25u)&&graphs>0,"All25 maps and live native graph coverage");std::cout<<"PASS maps="<<maps<<" graphs="<<graphs<<" exact_disk_ticks="<<frames<<" known_bodies="<<knownBodies<<" absent_bodies="<<absentBodies<<'\n';
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}
