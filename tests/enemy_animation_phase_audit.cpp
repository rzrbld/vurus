// Actual SceneRenderer native-phase protocol versus original431ae0/433b30
// gate/timer and4334e0 comparisons retained under n1-animation-phases.
#define main vyruzApplicationMain
#include "../src/main.cpp"
#undef main
#include "game/properties.h"
#include "game/save_file.h"
#include "game/save_state.h"
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace vyruz;
namespace {
void need(bool b,const std::string&s){if(!b)throw std::runtime_error(s);}
void replace(SharedProperties&p,const std::string&k,const std::string&v){std::erase_if(p.write(),[&](const auto&n){return contentNameIs(n.key,k);});TextNode n;n.kind=TextKind::Assignment;n.key=k;n.values={v};p.push_back(n);}
SceneChannelState body(const SceneRendererState&s){for(const auto&c:s.channels)if(!c.shadow&&!c.firing)return c;if(s.effects)for(const auto&c:s.effects->invisibleChannels)if(!c.channel.shadow&&!c.channel.firing)return c.channel;throw std::runtime_error("body missing");}
int counter(const SceneRendererState&s){need(s.effects&&!s.effects->enemyLeans.empty(),"native selector counter saved");return s.effects->enemyLeans.front().value;}
float visibility(const SceneRendererState&s){need(s.effects&&!s.effects->invisibleChannels.empty(),"derived invisible state");return s.effects->invisibleChannels.front().visibility;}
bool clockEqual(AnimationCursorState a,AnimationCursorState b){return a.cursor==b.cursor&&a.flags==b.flags&&a.remaining==b.remaining;}
std::string fingerprint(const SceneRendererState&s){std::string r;const auto add=[&](const auto&c){r+=std::to_string(c.sequence)+":"+std::to_string(c.cursor.cursor)+":"+std::to_string(c.cursor.flags)+":"+std::to_string(c.cursor.remaining)+";";};for(const auto&c:s.channels)add(c);if(s.effects){for(const auto&c:s.effects->invisibleChannels){add(c.channel);r+=std::to_string(c.visibility)+":"+std::to_string(c.visibilityAlpha)+";";}for(const auto&c:s.effects->enemyLeans)r+=std::to_string(c.object)+":"+std::to_string(c.value)+";";}return r;}
// Synthetic two-pose sound bank: tests public cursor/audio behavior without
// requiring a particular original sprite to contain sound commands.
std::vector<uint8_t> syncSoundBank() {
 using Bytes=std::vector<uint8_t>;
 const auto put=[](Bytes& b,size_t off,uint32_t value){for(int i=0;i<4;++i)b[off+i]=uint8_t(value>>(8*i));};
 const auto append=[](Bytes& b,const Bytes& tail){b.insert(b.end(),tail.begin(),tail.end());};
 const auto chunk=[&](const char* tag,const Bytes& payload,uint32_t children=0){Bytes b(16);std::copy_n(tag,4,b.begin());put(b,4,payload.size());put(b,12,children);append(b,payload);return b;};
 const auto streams=[&](const std::array<Bytes,4>& parts){Bytes b(96);for(size_t i=0;i<4;++i){put(b,i*24+16,parts[i].size());append(b,parts[i]);while(b.size()%4)b.push_back(0);}return b;};
 Bytes descriptor(44);put(descriptor,0,1);put(descriptor,4,1);put(descriptor,8,1);
 Bytes bitmap(72);std::copy_n("BMP_",4,bitmap.begin());put(bitmap,4,4);put(bitmap,8,0x40000322);put(bitmap,12,56);put(bitmap,16,0x40000322);put(bitmap,20,1);put(bitmap,24,16);put(bitmap,28,16);put(bitmap,32,1);put(bitmap,36,1);put(bitmap,40,4);bitmap[69]=0x7c;
 Bytes sequence(32);put(sequence,0,2);put(sequence,4,2);put(sequence,8,1);
 Bytes poses(56);for(size_t i=0;i<2;++i){put(poses,i*28,1);put(poses,i*28+16,1);put(poses,i*28+20,i+1);put(poses,i*28+24,1);}
 Bytes hg(36),ha(36);put(hg,0,1);put(ha,0,1);
 Bytes head=chunk("GOB_",hg);append(head,chunk("ANM_",ha));
 Bytes data=chunk("GOB_",streams({descriptor,bitmap,{},{}}));append(data,chunk("ANM_",streams({sequence,{},poses,{}})));
 Bytes root=chunk("HEAD",head,2);append(root,chunk("DATA",data,2));return chunk("GAF_",root,2);
}
unsigned syncTraversalControls(Gfx& gfx) {
 SceneRenderer renderer;SceneContent scene;scene.objects.resize(3);std::vector<ActorState> actors(3);std::string error;
 for(size_t i=0;i<3;++i){auto& o=scene.objects[i];o.id={uint32_t(i)};o.sprite.path="test/sync.gaf";o.sprite.sequence={1};replace(o.properties,"Sub-type","FX");actors[i].alive=true;}
 const auto bank=syncSoundBank();ContentReader read=[&](const auto& path,auto& bytes){if(path!="test/sync.gaf")return false;bytes=bank;return true;};
 need(renderer.load(scene,read,error),error);
 const auto clock=[&](size_t id){return *renderer.itemCursor(id,1);};
 const auto cues=[&](SceneAudioCueKind kind){std::vector<size_t> ids;for(const auto& cue:renderer.takeAudioCues())if(cue.kind==kind)ids.push_back(cue.object);return ids;};
 unsigned checks=0;
 renderer.advance(1,&actors);need(cues(SceneAudioCueKind::Play)==std::vector<size_t>({0,1,2}),"zero birth uses object ordering");++checks;
 renderer.advance(6,&actors);need(clock(0).remaining==7&&cues(SceneAudioCueKind::Held)==std::vector<size_t>({0,1,2}),"unchanged passive sync preserves cursor and held audio");++checks;
 actors[0].visualRevision=(uint64_t{5}<<32)|1;actors[1].visualRevision=(uint64_t{2}<<32)|1;actors[2].visualRevision=(uint64_t{5}<<32)|1;
 renderer.advance(1,&actors);need(cues(SceneAudioCueKind::Play)==std::vector<size_t>({1,0,2}),"changed birth keys preserve stable tie/audio order");++checks;
 renderer.advance(6,&actors);need(clock(0).remaining==7&&cues(SceneAudioCueKind::Held)==std::vector<size_t>({1,0,2}),"unchanged sorted passive channels keep advancing");++checks;
 actors[0].alive=false;renderer.advance(7,&actors);const auto dead=clock(0);const auto aliveCues=cues(SceneAudioCueKind::Play);need(dead.remaining==7&&aliveCues==std::vector<size_t>({1,2}),"dead passive channel freezes without suppressing live audio");++checks;
 actors[0].alive=true;actors[0].reusedRecord=true;actors[0].visualRevision=(uint64_t{9}<<32)|1;
 renderer.advance(7,&actors);need(clock(0).remaining==14,"recycled birth retains first-pass skip");renderer.takeAudioCues();++checks;
 renderer.advance(7,&actors);need(clock(0).remaining==7,"recycled passive cursor resumes next pass");renderer.takeAudioCues();++checks;
 actors[2].gate.emplace();actors[2].gateRevision=1;actors[2].gateState.sequence={1};renderer.syncActors(actors);need((clock(2).flags&8)&&clock(2).cursor==2,"runtime gate state bypasses passive no-op guard");++checks;
 const auto saved=renderer.capture();SceneRenderer restored;need(restored.load(scene,read,error)&&restored.restore(saved,error),error);renderer.takeAudioCues();
 for(int i=0;i<8;++i){renderer.advance(7,&actors);restored.advance(7,&actors);need(fingerprint(renderer.capture())==fingerprint(restored.capture()),"passive/recycled/gate cursor restore continuation");renderer.takeAudioCues();restored.takeAudioCues();++checks;}
 renderer.clear(gfx);restored.clear(gfx);return checks;
}

unsigned cachedOrderControls(Gfx& gfx) {
 SceneRenderer renderer;SceneContent scene;scene.objects.resize(48);std::vector<ActorState> actors(48);std::string error;
 for(size_t i=0;i<actors.size();++i){auto& o=scene.objects[i];o.id={uint32_t(i)};o.sprite={"test/sync.gaf",{1}};o.position={float(i*3),100};replace(o.properties,"Sub-type","FX");actors[i].alive=true;}
 const auto bank=syncSoundBank();ContentReader read=[&](const auto& path,auto& bytes){if(path!="test/sync.gaf")return false;bytes=bank;return true;};need(renderer.load(scene,read,error),error);
 Level level;level.mapW=level.mapH=64;uint32_t random=8141;unsigned controls=0;
 for(unsigned step=0;step<200;++step){
  std::vector<size_t> expected;
  for(size_t i=0;i<actors.size();++i){random=random*1664525u+1013904223u;actors[i].alive=(random%7)!=0;actors[i].visualRevision=(uint64_t((random>>8)%8)<<32)|(step+1);if(actors[i].alive)expected.push_back(i);}
  const auto key=[&](size_t i){const auto birth=actors[i].visualRevision>>32;return birth?birth-1:uint64_t(i);};std::stable_sort(expected.begin(),expected.end(),[&](size_t a,size_t b){return key(a)<key(b);});
  renderer.syncActors(actors);renderer.advance(1,&actors);renderer.takeAudioCues();
  for(int repeat=0;repeat<3;++repeat){need(renderer.sync(scene,error),error);renderer.syncActors(actors);}
  if(step%10==0){const auto saved=renderer.capture();need(renderer.restore(saved,error),error);++controls;}
  if(step%20==0){gfx.beginFrame(640,480);renderer.draw(gfx,scene,level,0,0,0,0,0,-1,[](const ForegroundTile&){},&actors);renderer.draw(gfx,scene,level,0,0,0,0,0,-1,[](const ForegroundTile&){},&actors,nullptr,1,nullptr,true);++controls;}
  renderer.advance(14,&actors);std::vector<size_t> actual;for(auto& cue:renderer.takeAudioCues())if(cue.kind==SceneAudioCueKind::Play)actual.push_back(cue.object);
  need(actual==expected,"random birth/death/tie order survives unchanged sync, repeated draws and restore");++controls;
 }
 renderer.advance(1,nullptr);renderer.takeAudioCues();renderer.syncActors(actors);renderer.advance(14,&actors);renderer.takeAudioCues();++controls;
 renderer.clear(gfx);return controls;
}

}
int main(int argc,char**argv){try{need(argc==3,"usage: enemy_animation_phase_audit DATA OUTPUT");std::filesystem::path out=argv[2];std::filesystem::create_directories(out);need(SDL_Init(SDL_INIT_VIDEO)==0,"SDL");App a;a.window=SDL_CreateWindow("Enemy native phases",0,0,640,480,SDL_WINDOW_OPENGL|SDL_WINDOW_HIDDEN);need(a.window&&a.gfx.init(a.window),"GL");need(a.vfs.init(argv[1]),a.vfs.error());ContentReader read=[&](const auto&p,auto&b){return a.vfs.read(p,b);};std::string error;std::map<std::string,std::pair<ObjectPlacement,SceneContent>> fixtures;
 for(const auto&map:debugLevels()){SceneContent s;need(loadSceneContent(read,map,s,error),error);for(const auto&o:s.objects){const auto f=objectSubtype(o);if(f=="spider"||f=="staticgen"||f=="heli"||f=="cop"||f=="burner")fixtures.try_emplace(f,std::pair{o,s});if(f=="cop"&&o.flags.invisible.value_or(false))fixtures.try_emplace("invisible-cop",std::pair{o,s});}}
 need(fixtures.size()==6,"six real original resource fixtures");std::ofstream report(out/"phases.tsv");report<<"fixture\tcontrols\tresult\n";unsigned controls=syncTraversalControls(a.gfx)+cachedOrderControls(a.gfx);
 for(const auto&[name,fixture]:fixtures){auto scene=fixture.second;auto o=fixture.first;o.id={0};o.position={320,280};std::erase_if(o.properties.write(),[](const auto&n){return contentNameIs(n.key,"add")&&!n.arguments.empty()&&contentNameIs(n.arguments[0],"fx");});scene.objects={o};scene.player.reset();scene.paths.clear();std::vector<ActorState>actors(1);auto&actor=actors[0];actor.alive=actor.solid=actor.enemy=actor.hudEnemy=actor.directional=actor.enemyRevealed=true;actor.walkingCommand=name=="spider";actor.direction=0;actor.enemyMagnitude=2.0001f;
  const int subtype=name=="heli"?0x1d:name=="burner"?0x1a:0x16;if(name!="spider"&&name!="staticgen"){actor.enemyTurnConfig={subtype,false,true,35,50};actor.enemyTurn=initializeEnemyTurn(actor.enemyTurnConfig,0,0);actor.enemyTurn->turnCommand=203;}
  a.sceneRenderer.clear(a.gfx);a.sceneRenderer.setNativeEnemyPhases();need(a.sceneRenderer.load(scene,read,error),error);NativeActorCells cells;cells.reset(64,64);cells.relocate(0,cells.coordinate(320,280));EnemyAnimationView view{0,0,640,480,64,64,&cells};view.visibilityStep=true;const auto initial=a.sceneRenderer.capture();
  // Native window: actual linked cell(12,19) admitted,13x/20y excluded.
  for(const auto&xy:{std::pair{12,19},std::pair{13,19},std::pair{12,20}}){need(a.sceneRenderer.restore(initial,error),error);cells.relocate(0,size_t(xy.first)*64+xy.second);const bool inside=xy.first<13&&xy.second<20;const auto before=fingerprint(a.sceneRenderer.capture());a.sceneRenderer.advanceEnemyPhase(17,actors,view,EnemyAnimationPhase::VisibleUpdate);a.sceneRenderer.advanceEnemyPhase(17,actors,view,EnemyAnimationPhase::Draw);if(!inside)need(fingerprint(a.sceneRenderer.capture())==before,"outside native expanded window freezes selectors/cursor/fade "+name);else need(fingerprint(a.sceneRenderer.capture())!=before,"inside native expanded window admits phases "+name);++controls;}
  // Membership, not projected sprite bounds, is authoritative. This manual
  // linkage is a bounded reader fixture; native movement maintains it in World.
  cells.erase(0);const auto missing=fingerprint(a.sceneRenderer.capture());a.sceneRenderer.advanceEnemyPhase(17,actors,view,EnemyAnimationPhase::VisibleUpdate);a.sceneRenderer.advanceEnemyPhase(17,actors,view,EnemyAnimationPhase::Draw);need(fingerprint(a.sceneRenderer.capture())==missing,"detached native actor not updated");++controls;cells.relocate(0,cells.coordinate(320,280));need(a.sceneRenderer.restore(initial,error),error);
  // Ordinary legacy advance must leave explicit enemy phases untouched.
  const auto held=fingerprint(a.sceneRenderer.capture());a.sceneRenderer.advance(1000,&actors,1000);need(fingerprint(a.sceneRenderer.capture())==held,"general advance cannot duplicate explicit enemy update "+name);++controls;
  if(name=="heli"){need(counter(a.sceneRenderer.capture())==0,"fresh native tilt counter");a.sceneRenderer.advanceEnemyPhase(1000,actors,view,EnemyAnimationPhase::VisibleUpdate);need(counter(a.sceneRenderer.capture())==1,"Heli cached magnitude single invocation despite1000ms");a.sceneRenderer.advanceEnemyPhase(0,actors,view,EnemyAnimationPhase::Draw);need(counter(a.sceneRenderer.capture())==2,"Heli selector runs draw phase even zeroelapsed");++controls;
   actor.enemyMagnitude=0;const auto before=counter(a.sceneRenderer.capture());a.sceneRenderer.advanceEnemyPhase(17,actors,view,EnemyAnimationPhase::Draw);need(counter(a.sceneRenderer.capture())==before+1,"Heli native cached zero magnitude threshold, not displacement");++controls;
  }else if(name=="cop"||name=="burner"||name=="invisible-cop"){a.sceneRenderer.advanceEnemyPhase(17,actors,view,EnemyAnimationPhase::VisibleUpdate);need(counter(a.sceneRenderer.capture())==-1,"lean selector in update phase");a.sceneRenderer.advanceEnemyPhase(17,actors,view,EnemyAnimationPhase::Draw);need(counter(a.sceneRenderer.capture())==(name=="burner"?-1:-2),"lean selector in draw phase");++controls;}
  if(name=="invisible-cop"){need(a.sceneRenderer.restore(initial,error),error);view.visibilityStep=false;a.sceneRenderer.advanceEnemyPhase(17,actors,view,EnemyAnimationPhase::Draw);need(visibility(a.sceneRenderer.capture())==0,"native worldflag100 clear freezes fade");view.visibilityStep=true;need(a.sceneRenderer.restore(initial,error),error);a.sceneRenderer.advanceEnemyPhase(1000,actors,view,EnemyAnimationPhase::VisibleUpdate);need(visibility(a.sceneRenderer.capture())==0,"update phase never advances reveal");a.sceneRenderer.advanceEnemyPhase(0,actors,view,EnemyAnimationPhase::Draw);need(visibility(a.sceneRenderer.capture())==1,"draw reveal one step independent ofelapsed");for(int i=1;i<49;++i)a.sceneRenderer.advanceEnemyPhase(0,actors,view,EnemyAnimationPhase::Draw);need(visibility(a.sceneRenderer.capture())==49&&a.sceneRenderer.capture().effects->invisibleChannels.front().visibilityAlpha,"49th reveal alpha");a.sceneRenderer.advanceEnemyPhase(0,actors,view,EnemyAnimationPhase::Draw);need(!a.sceneRenderer.capture().effects->invisibleChannels.front().visibilityAlpha,"50th reveal opaque");++controls;}
  if(name=="spider"||name=="staticgen"){need(a.sceneRenderer.restore(initial,error),error);GafAnimation bank;std::vector<uint8_t>bytes;const auto path=name=="staticgen"?o.shadow.path:o.sprite.path;need(read(path,bytes)&&decodeGafAnimation(bytes,bank),"oracle bank");const auto&seq=bank.sequences.at(name=="staticgen"?0:o.sprite.sequence.value-1);AnimationCursor expected;expected.reset(seq);a.sceneRenderer.advanceEnemyPhase(17,actors,view,EnemyAnimationPhase::VisibleUpdate);expected.advance(seq,17);need(clockEqual(body(a.sceneRenderer.capture()).cursor,expected.capture()),"first native timed phase");a.sceneRenderer.advanceEnemyPhase(17,actors,view,EnemyAnimationPhase::Draw);expected.advance(seq,17);need(clockEqual(body(a.sceneRenderer.capture()).cursor,expected.capture()),"second native timed phase, not once-per-tick adapter");++controls;}
  // Time Warp player-only update excludes enemies; draw remains the full mask.
  need(a.sceneRenderer.restore(initial,error),error);view.playerOnly=true;const auto timeHeld=fingerprint(a.sceneRenderer.capture());a.sceneRenderer.advanceEnemyPhase(17,actors,view,EnemyAnimationPhase::VisibleUpdate);need(fingerprint(a.sceneRenderer.capture())==timeHeld,"TimeWarp player-only update mask");a.sceneRenderer.advanceEnemyPhase(17,actors,view,EnemyAnimationPhase::Draw);need(fingerprint(a.sceneRenderer.capture())!=timeHeld,"TimeWarp draw still admits enemies");view.playerOnly=false;++controls;
  // Capture includes independent Heli counter as shared native21c presentation
  // counter; restoring offscreen must not consume a phase or infer from poses.
  const auto checkpoint=a.sceneRenderer.capture();const auto check=fingerprint(checkpoint);cells.relocate(0,size_t(63)*64+63);a.sceneRenderer.advanceEnemyPhase(17,actors,view,EnemyAnimationPhase::Draw);need(fingerprint(a.sceneRenderer.capture())==check,"offscreen saved phases remain held");cells.relocate(0,cells.coordinate(320,280));a.sceneRenderer.advanceEnemyPhase(17,actors,view,EnemyAnimationPhase::VisibleUpdate);a.sceneRenderer.advanceEnemyPhase(17,actors,view,EnemyAnimationPhase::Draw);const auto continuation=fingerprint(a.sceneRenderer.capture());need(a.sceneRenderer.restore(checkpoint,error),error);a.sceneRenderer.advanceEnemyPhase(17,actors,view,EnemyAnimationPhase::VisibleUpdate);a.sceneRenderer.advanceEnemyPhase(17,actors,view,EnemyAnimationPhase::Draw);need(fingerprint(a.sceneRenderer.capture())==continuation,"native phase live checkpoint continuation "+name);++controls;
  // Rendering a prepared phase cannot advance lean/tilt/fade a second time.
  Level l;l.mapW=l.mapH=64;a.gfx.beginFrame(640,480);a.gfx.clear(.2f,.3f,.4f);const auto drawHeld=fingerprint(a.sceneRenderer.capture());a.sceneRenderer.draw(a.gfx,scene,l,0,0,0,0,0,-1,[](const ForegroundTile&){},&actors);need(fingerprint(a.sceneRenderer.capture())==drawHeld,"prepared draw does not repeat native selector "+name);++controls;
  if(name=="spider"||name=="staticgen"||name=="heli"||name=="invisible-cop") {
   const auto capture=[&](const std::string& label){a.gfx.beginFrame(640,480);a.gfx.clear(.2f,.3f,.4f);a.sceneRenderer.draw(a.gfx,scene,l,0,0,0,0,0,-1,[](const ForegroundTile&){},&actors);need(saveScreenshot((out/(name+"-"+label+".ppm")).c_str(),640,480),"representative native phase capture");};
   if(name=="invisible-cop"){actor.enemyRevealed=true;for(int i=0;i<50;++i)a.sceneRenderer.advanceEnemyPhase(0,actors,view,EnemyAnimationPhase::Draw);capture("revealed");actor.enemyRevealed=false;for(int i=0;i<24;++i)a.sceneRenderer.advanceEnemyPhase(0,actors,view,EnemyAnimationPhase::Draw);capture("fading");}else capture("native-phase");
  }
  report<<name<<"\tnative window/membership/masks/clocks/livecheckpoint\tPASS\n";
 }
 // Mixed restore reorders persisted versus derived invisible channels;
 // native cell identity must continue addressing each object's rebuilt cache.
 {SceneContent scene;auto cop=fixtures.at("invisible-cop").first,heli=fixtures.at("heli").first;
  cop.id={0};cop.classId={0};heli.id={1};heli.classId={1};cop.position={320,280};heli.position={4032,2016};
  for(auto* o:{&cop,&heli})std::erase_if(o->properties.write(),[](const auto&n){return contentNameIs(n.key,"add")&&!n.arguments.empty()&&contentNameIs(n.arguments[0],"fx");});
  scene.objects={cop,heli};std::vector<ActorState> actors(2);for(size_t id=0;id<2;++id){auto&actor=actors[id];actor.alive=actor.solid=actor.enemy=actor.directional=actor.enemyRevealed=true;actor.enemyTurnConfig={id?0x1d:0x16,false,true,35,50};actor.enemyTurn=initializeEnemyTurn(actor.enemyTurnConfig,0,0);actor.enemyTurn->turnCommand=203;actor.enemyMagnitude=3;}
  a.sceneRenderer.clear(a.gfx);a.sceneRenderer.setNativeEnemyPhases();need(a.sceneRenderer.load(scene,read,error),error);const auto checkpoint=a.sceneRenderer.capture();need(a.sceneRenderer.restore(checkpoint,error),error);NativeActorCells cells;cells.reset(64,64);cells.relocate(0,cells.coordinate(320,280));cells.relocate(1,cells.coordinate(4032,2016));EnemyAnimationView view{0,0,640,480,64,64,&cells};view.visibilityStep=true;a.sceneRenderer.advanceEnemyPhase(17,actors,view,EnemyAnimationPhase::Draw);const auto result=a.sceneRenderer.capture();for(const auto&entry:result.effects->enemyLeans)need(entry.value==(entry.object==0?-1:0),"restored native object/channel index identity");for(const auto&entry:result.effects->invisibleChannels)need(entry.visibility==1,"restored mixed invisible body admission");++controls;
  // Restore can reorder derived invisible channels. Subsequent FX and enemy
  // births must preserve old channel ownership and bind the newborn correctly.
  auto fx=scene.objects[1];fx.id={2};replace(fx.properties,"Sub-type","FX");scene.objects.push_back(fx);
  actors.push_back(actors[1]);actors.back().effect=true;cells.relocate(2,cells.coordinate(350,280));
  need(a.sceneRenderer.sync(scene,error),error);const auto withFx=a.sceneRenderer.capture();
  for(const auto&entry:withFx.effects->enemyLeans)need(entry.value==(entry.object==0?-1:0),"FX birth preserves existing native counters");++controls;
  auto born=scene.objects[1];born.id={3};born.position={350,280};scene.objects.push_back(born);actors.push_back(actors[1]);cells.relocate(3,cells.coordinate(350,280));
  need(a.sceneRenderer.sync(scene,error),error);a.sceneRenderer.advanceEnemyPhase(17,actors,view,EnemyAnimationPhase::VisibleUpdate);a.sceneRenderer.advanceEnemyPhase(17,actors,view,EnemyAnimationPhase::Draw);
  const auto afterBirth=a.sceneRenderer.capture();bool newborn=false;
  for(const auto&entry:afterBirth.effects->enemyLeans){need(entry.value==(entry.object==0?-3:entry.object==3?2:0),"newborn and restored native counters keep independent phase admission");newborn|=entry.object==3;}need(newborn,"newborn Heli bound after non-enemy birth");++controls;
  need(a.sceneRenderer.restore(afterBirth,error),error);a.sceneRenderer.advanceEnemyPhase(17,actors,view,EnemyAnimationPhase::Draw);
  const auto afterRestore=a.sceneRenderer.capture();for(const auto&entry:afterRestore.effects->enemyLeans)need(entry.value==(entry.object==0?-4:entry.object==3?3:0),"post-birth live restore retains native channel ownership");++controls;

 }
 // Genuine v35 disk codec keeps ordinary channel banks/cursors, but omits
 // live effects (native disk recreates transient fade/lean/tilt). Test exact
 // live continuation separately from deterministic disk-recreated continuation.
 {auto scene=fixtures.at("invisible-cop").second;auto player=scene.objects.at(scene.player->value),cop=fixtures.at("invisible-cop").first,heli=fixtures.at("heli").first;
  auto heliClass=fixtures.at("heli").second.classes.at(heli.classId.value);heliClass.id={uint32_t(scene.classes.size())};scene.classes.push_back(heliClass);heli.classId=heliClass.id;
  player.id={0};cop.id={1};heli.id={2};player.position={800,800};cop.position={320,280};heli.position={350,280};
  for(auto*o:{&cop,&heli})std::erase_if(o->properties.write(),[](const auto&n){return contentNameIs(n.key,"add")&&!n.arguments.empty()&&contentNameIs(n.arguments[0],"fx");});scene.objects={player,cop,heli};scene.player=ObjectId{0};scene.paths.clear();scene.mapPath="TEST/ENEMY-PHASE-DISK.3DG";
  Level terrain;terrain.mapW=terrain.mapH=64;terrain.hasTerrainCollision=true;GameWorld world;world.setEffectReader(read);need(world.load(scene,terrain,error),error);auto actors=world.actors();actors[1].enemyRevealed=true;actors[2].enemyMagnitude=3;
  SceneRenderer current;current.setNativeEnemyPhases();need(current.load(scene,read,error),error);EnemyAnimationView view{0,0,640,480,64,64,&world.nativeActorCells()};view.visibilityStep=true;
  const auto paired=[&](SceneRenderer& renderer){renderer.advanceEnemyPhase(17,actors,view,EnemyAnimationPhase::VisibleUpdate);renderer.advanceEnemyPhase(17,actors,view,EnemyAnimationPhase::Draw);};
  const auto heliCounter=[&](const SceneRendererState& state){need(state.effects.has_value(),"native live effects snapshot");for(const auto&counter:state.effects->enemyLeans)if(counter.object==2)return counter.value;throw std::runtime_error("visible Heli native21c counter missing");};
  const auto copVisibility=[&](const SceneRendererState& state){need(state.effects.has_value(),"native live fade snapshot");for(const auto&channel:state.effects->invisibleChannels)if(channel.channel.object==1&&!channel.channel.shadow)return channel.visibility;throw std::runtime_error("invisible Cop live channel missing");};
  for(int tick=0;tick<7;++tick)paired(current);const auto live=current.capture();need(heliCounter(live)==14&&copVisibility(live)==7,"checkpoint retains independent Heli21c and invisible draw counter");
  // Old N3 live snapshots did not include visible Heli21c in enemyLeans.
  auto oldLive=live;std::erase_if(oldLive.effects->enemyLeans,[](const auto&counter){return counter.object==2;});need(current.restore(oldLive,error),error);need(heliCounter(current.capture())==10,"old missing Heli counter uses documented selected-bank migration");need(copVisibility(current.capture())==7,"old missing Heli counter retains unrelated invisible channel");++controls;
  need(current.restore(live,error),error);const auto before=fingerprint(current.capture());
  for(int malformed=0;malformed<4;++malformed){auto bad=live;if(malformed==0){for(const auto&counter:live.effects->enemyLeans)if(counter.object==2)bad.effects->enemyLeans.push_back(counter);}else if(malformed==1||malformed==2){for(auto&counter:bad.effects->enemyLeans)if(counter.object==2)counter.value=malformed==1?26:-1;}else bad.effects->invisibleChannels.front().visibility=50;need(!current.restore(bad,error),"malformed native presentation state rejected");need(fingerprint(current.capture())==before,"malformed phase restore is transactional");++controls;}
  auto limit=live;for(auto&counter:limit.effects->enemyLeans)if(counter.object==2)counter.value=25;need(current.restore(limit,error),error);paired(current);need(heliCounter(current.capture())==25,"native Heli25 limit remains saturated");++controls;need(current.restore(live,error),error);
  SaveGame save;save.currentLevel=scene.mapPath;save.levels[scene.mapPath].world=world.snapshot();save.levels[scene.mapPath].render=live;std::vector<uint8_t> packet;SaveGame decoded;need(encodeSave(save,packet,error)&&decodeSave(packet,decoded,error),error);need(packet[8]==36&&!decoded.levels.at(scene.mapPath).render.effects,"current v36 codec deliberately omits live transient counters and invisible channels");
  const auto&disk=decoded.levels.at(scene.mapPath).render;need(disk.objectCount==live.objectCount&&disk.channels.size()==live.channels.size(),"disk preserves ordinary channel identities");for(size_t i=0;i<disk.channels.size();++i)need(disk.channels[i].object==live.channels[i].object&&disk.channels[i].sequence==live.channels[i].sequence&&clockEqual(disk.channels[i].cursor,live.channels[i].cursor),"disk preserves exact ordinary selected bank/cursor");++controls;
  SceneRenderer resumedA,resumedB;for(auto*r:{&resumedA,&resumedB}){r->setNativeEnemyPhases();need(r->load(scene,read,error)&&r->restore(disk,error),error);need(heliCounter(r->capture())==10&&copVisibility(r->capture())==0,"disk resets transient native presentation and reconstructs selected Heli bank");}
  for(int tick=0;tick<60;++tick){paired(resumedA);paired(resumedB);need(fingerprint(resumedA.capture())==fingerprint(resumedB.capture()),"genuine v35 disk-recreated native phase continuation");++controls;}
  // Live snapshot continuation retains14, unlike the explicit disk reset10.
  need(current.restore(live,error),error);SceneRenderer memory;memory.setNativeEnemyPhases();need(memory.load(scene,read,error)&&memory.restore(live,error),error);for(int tick=0;tick<60;++tick){paired(current);paired(memory);need(fingerprint(current.capture())==fingerprint(memory.capture()),"live native21c/invisible continuation exact");++controls;}
  std::ofstream codec(out/"disk-semantics.tsv");codec<<"schema\tlive_Heli21c\tdisk_Heli21c_migration\tlive_Cop_visibility\tdisk_Cop_visibility_reset\tcontinuation_ticks\n35\t14\t10\t7\t0\t120\n";for(auto*r:{&current,&resumedA,&resumedB,&memory})r->clear(a.gfx);
 }
 // Graphic-update-needed is absent from all shipped N1 contexts; preserve its
 // actual native caller contract in one explicitly synthetic Spider control.
 {auto scene=fixtures.at("spider").second;auto o=fixtures.at("spider").first;o.id={0};o.position={320,280};replace(scene.classes[o.classId.value].properties,"Graphic-update-needed","1");scene.objects={o};scene.player.reset();a.sceneRenderer.clear(a.gfx);a.sceneRenderer.setNativeEnemyPhases();need(a.sceneRenderer.load(scene,read,error),error);std::vector<ActorState>actors(1);actors[0].alive=actors[0].directional=actors[0].walkingCommand=true;NativeActorCells cells;cells.reset(64,64);cells.relocate(0,size_t(63)*64+63);EnemyAnimationView view{0,0,640,480,64,64,&cells};const auto before=body(a.sceneRenderer.capture()).cursor;a.sceneRenderer.advanceEnemyPhase(17,actors,view,EnemyAnimationPhase::VisibleUpdate);need(clockEqual(before,body(a.sceneRenderer.capture()).cursor),"Graphic excluded visible update");a.sceneRenderer.advanceEnemyPhase(17,actors,view,EnemyAnimationPhase::AwakeUpdate,0);need(!clockEqual(before,body(a.sceneRenderer.capture()).cursor),"Graphic awake update independent ofcamera");cells.relocate(0,cells.coordinate(320,280));const auto awake=body(a.sceneRenderer.capture()).cursor;a.sceneRenderer.advanceEnemyPhase(17,actors,view,EnemyAnimationPhase::Draw);need(clockEqual(awake,body(a.sceneRenderer.capture()).cursor),"Graphic draw does not advance timer again");++controls;}
 std::cout<<"PASS "<<controls<<" actual enemy renderer phase controls; native cells, paired clocks, cached Heli magnitude, lean/reveal phase, TimeWarp mask and livecheckpoint\n";a.sceneRenderer.clear(a.gfx);a.gfx.shutdown();SDL_DestroyWindow(a.window);SDL_Quit();return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
