// Opt-in real-resource save responsiveness and transaction audit.
#define main vyruzApplicationMain
#include "../src/main.cpp"
#undef main
#include "game/properties.h"
#include <iostream>
#include <stdexcept>
using Clock=std::chrono::steady_clock;
static void need(bool ok,const std::string& why){if(!ok)throw std::runtime_error(why);}
static std::vector<uint8_t> fileBytes(const std::string& path){std::ifstream f(path,std::ios::binary);return {(std::istreambuf_iterator<char>(f)),{}};}
static double ms(Clock::time_point start){return std::chrono::duration<double,std::milli>(Clock::now()-start).count();}
int main(int argc,char**argv){try{
 need(argc==3,"usage: save_async_audit DATA OUTPUT");
 const std::filesystem::path out=argv[2];std::filesystem::create_directories(out);
 SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS,"1");
 need(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_GAMECONTROLLER)==0,SDL_GetError());App a;
#if VYRUZ_GLES
 SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_ES);SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,3);SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,0);
#endif
 a.window=SDL_CreateWindow("Async save audit",0,0,1280,800,SDL_WINDOW_OPENGL|SDL_WINDOW_HIDDEN);
 need(a.window&&a.gfx.init(a.window),SDL_GetError());need(a.vfs.init(argv[1]),a.vfs.error());
 a.optionsPath=(out/"options.cfg").string();a.savePath=(out/"slot.vzs").string();a.scorePath=(out/"scores").string();
 a.saveHistory=true;a.retryPersistence=false;
 need(loadLevelData(a,"CAVE/T1C1.3DG"),a.loadError);a.presentation.close();a.menu.close(a.paused);
 constexpr size_t tower=88,zone=89;
 need(vyruz::propertyBool(a.content.objects[tower].properties,"Saving-plot"),"authored tower");
 auto baseline=a.world.snapshot();
 baseline.actors[baseline.player].indestructible=true;baseline.actors[baseline.player].vitals.money=100000;
 std::string error;
 const auto enter=[&]{
  need(a.world.restoreSnapshot(baseline,error),error);a.world.setDebugEnemyAI(false);a.world.setDebugGodMode(true);a.world.setPlayerPosition(3110.05f,2981.03f);
  for(int i=0;i<340&&!a.world.saveRequest();++i)a.world.tick(1.f/60,{});
  need(a.world.saveRequest()&&a.world.saveRequest()->cost==100000,"authored paid request");
 };
 enter();a.worldX=3050;a.worldY=2976;render(a);a.worldX=3110.05f;a.worldY=2981.03f;render(a);SDL_FlushEvents(SDL_FIRSTEVENT,SDL_LASTEVENT);a.inputFocused=true;
 vyruz::SavedLevel current;need(captureLevel(a,current,error),error);
 // Only the current level and entry checkpoint belong in new saves.
 vyruz::SaveGame expected;expected.currentLevel=levelKey(a.content.mapPath);expected.restartLevel=a.restartLevel;
 auto charged=current;charged.world.actors[charged.world.player].vitals.money-=100000;charged.world.message.clear();expected.levels[expected.currentLevel]=charged;
 std::vector<uint8_t> expectedBytes;need(vyruz::encodeStoredSave(expected,expectedBytes,error),error);
 expected={};
 const auto syncStart=Clock::now();need(saveSession(a,100000,error),error);const auto synchronousMs=ms(syncStart);
 need(fileBytes(a.lastSavedPath)==expectedBytes,"synchronous reference file");
 // Model a catalog reader holding storage: interactive capture/launch/input
 // must still return, and the fee remains reserved until publication succeeds.
 std::unique_lock storageGate(a.saveStorageMutex);
 const auto start=Clock::now();serviceSave(a,true);const auto launchMs=ms(start);
 need(a.saveFuture.valid()&&a.world.player().vitals.money==100000,"launch returns with fee uncharged");
 a.menuAction=vyruz::MenuAction::Restart;need(!applyMenuAction(a)&&a.menuAction==vyruz::MenuAction::Restart,"restart waits without consuming queued action");a.menuAction=vyruz::MenuAction::None;
 a.world.setPlayerPosition(3050,2976);a.world.tick(1.f/60,{});
 need(a.world.saveRequest().has_value(),"departure preserves in-flight transaction");
 a.world.toggleMap();need(!a.world.mapOpen(),"pending fee cannot be spent on map");
 const auto timeBefore=a.world.elapsedMilliseconds();size_t frames=0;std::vector<double> framesMs;
 while(a.saveFuture.valid()){
  const auto frame=Clock::now();handleInput(a);advanceSceneAnimation(a,kMoveDt*1000,a.world.elapsedMilliseconds());const auto updateMs=ms(frame);render(a);
  const auto drawMs=ms(frame);serviceSave(a,true);framesMs.push_back(ms(frame));
  if(framesMs.back()>16.7)std::cout<<"slow_frame="<<frames<<" update_ms="<<updateMs<<" draw_ms="<<drawMs-updateMs<<" poll_ms="<<framesMs.back()-drawMs<<"\n";
  if(frames==0)need(saveScreenshot((out/"saving.ppm").c_str(),1280,800),"saving capture");
  ++frames;
  if(frames==2){need(a.world.player().vitals.money==100000,"waiting for catalog storage cannot charge before commit");storageGate.unlock();}
  need(ms(start)<30000,"background save completes");SDL_Delay(1);
 }
 need(a.saveCatalogDirty,"gameplay rendering must not decode the save catalog");
 need(frames>1&&a.world.elapsedMilliseconds()>timeBefore,"gameplay and rendering advance while saving");
 need(a.world.player().vitals.money==0&&!a.world.saveRequest(),"success charges once after commit");
 const auto paidAudio=a.world.takeAudioEvents();need(std::count_if(paidAudio.begin(),paidAudio.end(),[](const auto& e){return e.kind==vyruz::AudioEventKind::SaveCash;})==1,"async paid save emits exactly one MONEY request");
 const auto written=a.lastSavedPath;need(fileBytes(written)==expectedBytes,"async file equals synchronous checkpoint exactly");
 serviceSave(a,true);need(a.world.player().vitals.money==0,"poll cannot double-charge");
 need(a.world.takeAudioEvents().empty(),"async completion poll cannot repeat MONEY");
 vyruz::SaveGame disk;need(vyruz::readSaveFile(written,disk,error),error);
 need(disk.levels.size()==1,"save contains only current level");
 // Disk continuation versus the exact source checkpoint, without replaying the
 // later live frames that intentionally continued during the background write.
 auto normalized=charged.world;vyruz::normalizeSavedSurprise(normalized);
 const auto trace=[&](const vyruz::WorldSnapshot& state){
  need(a.world.restoreSnapshot(state,error),error);
  for(int i=0;i<12;++i){vyruz::WorldInput input;input.movement.forward=1;a.world.tick(1.f/60,input);}
  vyruz::SaveGame s;s.currentLevel=levelKey(a.content.mapPath);s.levels[s.currentLevel].world=a.world.snapshot();
  std::vector<uint8_t>b;need(vyruz::encodeSave(s,b,error),error);return b;
 };
 need(trace(normalized)==trace(disk.levels.at(disk.currentLevel).world),"disk continuation matches source checkpoint");
 // A write failure after capture must retain both live cash and existing saves.
 enter();const auto oldPath=a.savePath;a.savePath=(std::filesystem::path(written)/"invalid-parent"/"slot.vzs").string();
 serviceSave(a,true);need(a.saveFuture.valid(),"failed destination still handled asynchronously");
 a.saveFuture.wait();serviceSave(a,true);
 need(a.world.player().vitals.money==100000&&!a.world.saveRequest(),"write failure does not charge");
 const auto failedAudio=a.world.takeAudioEvents();need(std::none_of(failedAudio.begin(),failedAudio.end(),[](const auto& e){return e.kind==vyruz::AudioEventKind::SaveCash;}),"failed async save cannot request MONEY");
 need(fileBytes(written)==expectedBytes,"write failure preserves existing checkpoint");
 a.savePath=oldPath;
 // Automatic entry checkpoints also finish off-thread, without a paid-save
 // message or debit; shutdown-style joining leaves a readable retry record.
 a.retryPersistence=true;a.restartPath=levelKey(a.content.mapPath);
 const auto retryStart=Clock::now();persistRetrySession(a,true);const auto retryLaunch=ms(retryStart);
 need(a.saveFuture.valid(),"automatic retry starts in background");
 a.saveFuture.wait();serviceSave(a,true);
 vyruz::SaveGame retry;need(vyruz::readDefaultRetry(a.savePath,a.options.storageProfile(),retry,error),error);
 need(a.world.player().vitals.money==100000,"automatic retry does not charge");
 need(retry.levels.at(retry.currentLevel).world.elapsedMs==a.restartLevel->world.elapsedMs,"retry preserves level entry checkpoint");
 std::cout<<"retry_launch_ms="<<retryLaunch<<"\n";
 std::sort(framesMs.begin(),framesMs.end());
 std::cout<<"PASS synchronous_ms="<<synchronousMs<<" launch_ms="<<launchMs<<" concurrent_frames="<<frames<<" frame_p50_ms="<<framesMs[framesMs.size()/2]
          <<" frame_p95_ms="<<framesMs[std::min(framesMs.size()-1,framesMs.size()*95/100)]<<" frame_p99_ms="<<framesMs[std::min(framesMs.size()-1,framesMs.size()*99/100)]<<" frame_max_ms="<<framesMs.back()<<" save_bytes="<<expectedBytes.size()<<"\n";
#if VYRUZ_GLES
 // A shutdown/error before the requested frame must still commit the snapshot.
 enter();serviceSave(a,true);need(a.saveFuture.valid(),"shutdown save starts");
 cancelSavePreview(a);
 need(a.saveFuture.wait_for(std::chrono::seconds(30))==std::future_status::ready,"cancelled preview cannot deadlock shutdown");
 serviceSave(a,true);need(a.world.player().vitals.money==0&&!a.world.saveRequest(),"cancelled preview preserves paid save success");
 vyruz::SaveGame interrupted;need(vyruz::readSaveFile(a.lastSavedPath,interrupted,error),error);
 auto noPreview=std::filesystem::path(a.lastSavedPath);noPreview.replace_extension(".tga");need(!std::filesystem::exists(noPreview),"cancelled preview does not reuse stale pixels");
#endif
 // Synchronous tooling uses the same successful-debit event boundary.
 enter();a.saveHistory=false;a.savePath=(out/"sync.vzs").string();a.world.takeAudioEvents();
 need(serviceSave(a,false),"synchronous paid save completes");
 const auto syncAudio=a.world.takeAudioEvents();
 need(a.world.player().vitals.money==0&&std::count_if(syncAudio.begin(),syncAudio.end(),[](const auto& e){return e.kind==vyruz::AudioEventKind::SaveCash;})==1,"synchronous save debits and requests MONEY once");
 a.sceneRenderer.clear(a.gfx);a.hud.clear(a.gfx);a.gfx.shutdown();SDL_DestroyWindow(a.window);SDL_Quit();return 0;
}catch(const std::exception&e){std::cerr<<"async save audit: "<<e.what()<<"\n";return 1;}}
