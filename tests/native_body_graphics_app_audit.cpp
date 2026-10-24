// Optional original-resource application audit. No production test hooks.
#define main vyruzApplicationMain
#include "../src/main.cpp"
#undef main
#include "game/save_file.h"
#include <fstream>
#include <stdexcept>
using namespace vyruz;
static void auditNeed(bool ok,const std::string& why){if(!ok)throw std::runtime_error(why);}
static bool auditClockEqual(AnimationCursorState a,AnimationCursorState b){return a.cursor==b.cursor&&a.flags==b.flags&&a.remaining==b.remaining;}
namespace vyruz {
struct AmmoImpactNativeTestAccess {
 static void heading(GameWorld&w,size_t id,unsigned h){w.actors_.at(id).direction=h;}
 static void step(GameWorld&w,int32_t ms){w.bodyGraphicsStepMs_=ms;}
 static void selected(GameWorld&w,size_t id,int32_t ms){w.advanceBodyGraphics(id,ms);}
 static void syntheticCue(GameWorld&w,size_t id){w.bodyGraphicsAudio_.push_back({id,w.scene_->objects[id].sprite.path,{NativeBodyGraphicsAudioKind::Entered,0,1,1,uint64_t{1}<<63,1,1}});}
 static bool pinned(const GameWorld&w,size_t id){return w.bodyGraphicsAudioPins_.contains(id);}
};
}
using AuditBodyClocks=std::map<size_t,NativeBodyGraphicsState>;
static AuditBodyClocks auditClocks(const GameWorld&w){AuditBodyClocks out;for(size_t i=0;i<w.actors().size();++i)if(w.actors()[i].bodyGraphics)out.emplace(i,*w.actors()[i].bodyGraphics);return out;}
static bool auditClocksEqual(const AuditBodyClocks&a,const AuditBodyClocks&b){if(a.size()!=b.size())return false;for(const auto&[id,x]:a){const auto at=b.find(id);if(at==b.end())return false;const auto&y=at->second;if(x.binding!=y.binding||x.kind!=y.kind||x.baseSequence!=y.baseSequence)return false;for(size_t h=0;h<24;++h)if(x.slots[h].sequence!=y.slots[h].sequence||!auditClockEqual(x.slots[h].animation,y.slots[h].animation))return false;}return true;}
static std::vector<uint8_t> auditWorld(const App&a){SaveGame s;s.currentLevel=a.content.mapPath;s.levels[s.currentLevel].world=a.world.snapshot();std::vector<uint8_t> bytes;std::string error;auditNeed(encodeSave(s,bytes,error),error);return bytes;}
static void auditDirectDraw(App&a){const auto p=a.world.playerPosition();a.sceneRenderer.draw(a.gfx,a.content,a.level,a.camX,a.camY,p.x,p.y,a.world.player().direction,-1,[](const ForegroundTile&){},&a.world.actors(),nullptr,1,&a.world.surprise(),false,a.world.renderActorRevision());}
static SceneChannelState auditChannel(const SceneRenderer&r,size_t id){for(const auto&c:r.capture().channels)if(c.object==id&&!c.shadow&&!c.firing)return c;throw std::runtime_error("missing body channel "+std::to_string(id));}
struct AuditClass {ClassId id;GafAnimation bank;};
static AuditClass auditFindClass(App&a,bool ammo){
 for(const auto&c:a.content.classes){ObjectPlacement object;object.classId=c.id;object.sprite=c.sprite;object.shadow=c.shadow;object.properties=c.properties;object.behaviours=c.behaviours;object.flags=c.flags;if(nativeObjectSubtype(object)!=(ammo?0x10u:0x15u)||c.flags.invisible.value_or(false)||c.sprite.path.empty()||!c.sprite.sequence.value||(!ammo&&propertyBool(c.properties,"Graphic-update-needed")))continue;
  std::vector<uint8_t> data;GafAnimation bank;if(!a.vfs.read(c.sprite.path,data)||!decodeGafAnimation(data,bank,GafDecodeMode::Metadata))continue;
  const auto base=c.sprite.sequence.value;if(base+(ammo?23:0)>bank.sequences.size())continue;
  const auto&s=bank.sequences[base-1];if(s.subframes.empty()||s.subframes.front().duration<2)continue;
  return {c.id,std::move(bank)};
 }
 throw std::runtime_error(ammo?"original directional Ammo fixture missing":"original nonGraphic Thing fixture missing");
}
static void auditModal(App&a,size_t thing,const std::string& name,const std::function<void()>& open,const std::function<void()>& close){open();const auto before=auditClocks(a.world);for(int n=0;n<3;++n)render(a,1);auditNeed(auditClocksEqual(before,auditClocks(a.world)),name+" redraw changed World body clocks");auditNeed(bodyGraphicsView(a,a.camX,a.camY).suspended,name+" view must be suspended");close();(void)thing;}
int main(int argc,char**argv)try{
 auditNeed(argc==3,"usage: native_body_graphics_app_audit DATA OUTPUT");const std::filesystem::path out=argv[2];std::filesystem::create_directories(out);
 auditNeed(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_AUDIO)==0,SDL_GetError());
 App a;a.retryPersistence=false;a.saveHistory=false;a.savePath=(out/"pre-render.vzs").string();
 a.window=SDL_CreateWindow("Native body graphics App audit",0,0,640,480,SDL_WINDOW_OPENGL|SDL_WINDOW_HIDDEN);auditNeed(a.window&&a.gfx.init(a.window),"GL initialization");a.gfx.setSwapEnabled(false);auditNeed(a.vfs.init(argv[1]),a.vfs.error());
 auditNeed(loadLevelData(a,"CAVE/T1C1.3DG",nullptr,false,false,false,1),a.loadError);a.presentation.close();a.menu.close(a.paused);a.paused=false;a.sessionStarted=true;a.world.setDebugGodMode(true);a.world.setDebugEnemyAI(false);
 const auto thingClass=auditFindClass(a,false),ammoClass=auditFindClass(a,true);const auto p=a.world.playerPosition();
 const auto thing=a.world.spawn(thingClass.id,{p.x+40,p.y+12}),ammo=a.world.spawn(ammoClass.id,{p.x-40,p.y+12});
 auditNeed(thing<a.world.actors().size()&&ammo<a.world.actors().size(),"fixture birth");std::string error;auditNeed(a.sceneRenderer.sync(a.content,error),error);a.sceneRenderer.syncActors(a.world.actors());
 auditNeed(a.world.actors()[thing].bodyGraphics&&a.world.actors()[ammo].bodyGraphics,"known body state");auditNeed(a.world.actors()[thing].bodyGraphics->binding==NativeBodyGraphicsBinding::Present&&a.world.actors()[ammo].bodyGraphics->binding==NativeBodyGraphicsBinding::Present,"original bank binding");
 std::ofstream log(out/"results.txt");log<<"map=CAVE/T1C1.3DG Thing="<<a.content.classes[thingClass.id.value].name<<" Ammo="<<a.content.classes[ammoClass.id.value].name<<'\n';
 // Selected-only directional clocks return to their earlier independent state.
 const auto initial=*a.world.actors()[ammo].bodyGraphics;AmmoImpactNativeTestAccess::heading(a.world,ammo,0);AmmoImpactNativeTestAccess::selected(a.world,ammo,1);const auto first=*a.world.actors()[ammo].bodyGraphics;
 auditNeed(!auditClockEqual(initial.slots[0].animation,first.slots[0].animation),"selected Ammo clock did not advance");for(unsigned h=1;h<24;++h)auditNeed(auditClockEqual(initial.slots[h].animation,first.slots[h].animation),"inactive Ammo heading advanced");
 AmmoImpactNativeTestAccess::heading(a.world,ammo,1);AmmoImpactNativeTestAccess::selected(a.world,ammo,1);AmmoImpactNativeTestAccess::heading(a.world,ammo,0);
 const auto&returned=*a.world.actors()[ammo].bodyGraphics;auditNeed(auditClockEqual(first.slots[0].animation,returned.slots[0].animation),"heading return reset old slot");
 const auto*pose=nativeBodyGraphicsFrame(returned,ammoClass.bank,0);auditNeed(pose!=nullptr,"returned selected pose missing");
 // Scene composition must mirror the authoritative state and emit no duplicate
 // body audio. It must not itself mutate any serialized World state.
 a.sceneRenderer.takeAudioCues();const auto beforeDraw=auditWorld(a);auditDirectDraw(a);auditNeed(beforeDraw==auditWorld(a),"SceneRenderer::draw mutated World");for(const auto&cue:a.sceneRenderer.takeAudioCues())auditNeed(cue.object!=thing&&cue.object!=ammo,"Scene duplicated authoritative body audio");
 // Active App redraw uses retained dt for new nonGraphic body clocks only.
 AmmoImpactNativeTestAccess::heading(a.world,thing,0);AmmoImpactNativeTestAccess::step(a.world,1);const auto activeBefore=a.world.actors()[thing].bodyGraphics->slots[0].animation;
 render(a,1);const auto activeFirst=a.world.actors()[thing].bodyGraphics->slots[0].animation;render(a,1);const auto activeSecond=a.world.actors()[thing].bodyGraphics->slots[0].animation;
 auditNeed(!auditClockEqual(activeBefore,activeFirst)&&!auditClockEqual(activeFirst,activeSecond),"active redraw did not use retained body dt");
 auditModal(a,thing,"pause",[&]{a.paused=true;},[&]{a.paused=false;});
 auditModal(a,thing,"inventory",[&]{a.inventory.toggle(a.paused);},[&]{a.inventory.close(a.paused);});
 auditModal(a,thing,"menu",[&]{a.menu.show(a.paused,false);},[&]{a.menu.close(a.paused);});
 // World Unknown does not suppress Scene's existing fallback when Scene can
 // read the actual bank. This is intentionally a separate metadata-owner setup.
 {
  auto unknownScene=a.content;GameWorld unknown;auditNeed(unknown.load(unknownScene,a.level,error),error);auditNeed(unknown.actors()[thing].bodyGraphics&&unknown.actors()[thing].bodyGraphics->binding==NativeBodyGraphicsBinding::Unknown,"reader-unset World must remain Unknown");
  SceneRenderer renderer;ContentReader read=[&](const auto&path,auto&data){return a.vfs.read(path,data);};auditNeed(renderer.load(unknownScene,read,error),error);renderer.syncActors(unknown.actors());renderer.advance(1,&unknown.actors());const auto old=auditChannel(renderer,thing).cursor;renderer.advance(1,&unknown.actors());const auto next=auditChannel(renderer,thing).cursor;auditNeed(!auditClockEqual(old,next),"Unknown World incorrectly suppressed Scene fallback");renderer.clear(a.gfx);
 }
 // Device-unavailable consumer still drains the pending producer vector and
 // releases the pin set protecting actor routes until consume completes.
 auditNeed(!a.audio.device().available(),"fixture expects unopened audio device");a.world.discardBodyGraphicsAudio();a.pendingAnimationAudio.clear();AmmoImpactNativeTestAccess::syntheticCue(a.world,thing);collectBodyAnimationAudio(a);auditNeed(!a.pendingAnimationAudio.empty()&&AmmoImpactNativeTestAccess::pinned(a.world,thing),"drained body cue did not retain route pin");consumeGameplayAudio(a);auditNeed(a.pendingAnimationAudio.empty()&&!AmmoImpactNativeTestAccess::pinned(a.world,thing),"unavailable device leaked pending cue/pin");auditNeed(a.world.takeAudioEvents().empty(),"unavailable device did not drain World event queue");
 render(a,1); // Replace the last modal framebuffer with the active World view.
 auditNeed(saveScreenshot((out/"body-graphics.ppm").c_str(),a.gfx.drawableWidth(),a.gfx.drawableHeight()),"capture body graphics image");
 // Actual App save cadence: capture before final render, clear interpolation as
 // savedThisFrame does, render now; loadSession also renders immediately BEFORE
 // another simulation tick. Comparing without that initial draw is invalid.
 a.previousPositions.clear();AmmoImpactNativeTestAccess::step(a.world,1);auditNeed(saveSession(a,0,error),error);SavedLevel captured;auditNeed(captureLevel(a,captured,error),error);render(a,1);const auto expectedInitial=auditClocks(a.world);
 a.world.tick(.014f,{});advanceSceneAnimation(a,14,a.world.elapsedMilliseconds());a.previousPositions.clear();render(a,1);const auto expectedNext=auditClocks(a.world);consumeGameplayAudio(a);
 auditNeed(loadSession(a,a.savePath),a.loadError);a.presentation.close();a.menu.close(a.paused);a.paused=false;a.sessionStarted=true;a.world.setDebugGodMode(true);a.world.setDebugEnemyAI(false);a.previousPositions.clear();
 render(a,1);auditNeed(auditClocksEqual(expectedInitial,auditClocks(a.world)),"App pre-render save/load immediate draw clock mismatch");a.world.tick(.014f,{});advanceSceneAnimation(a,14,a.world.elapsedMilliseconds());a.previousPositions.clear();render(a,1);auditNeed(auditClocksEqual(expectedNext,auditClocks(a.world)),"App pre-render save/load next-tick draw clock mismatch");consumeGameplayAudio(a);
 log<<"PASS selected heading return, World draw immutability, modal freeze, active redraw, Unknown fallback, unavailable-device pin drain, pre-render App save/load immediate+next-tick clocks\n";
 a.sceneRenderer.clear(a.gfx);a.hud.clear(a.gfx);a.gfx.shutdown();SDL_DestroyWindow(a.window);SDL_Quit();return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}
