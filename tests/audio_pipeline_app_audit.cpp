// Opt-in actual-resource App/audio integration. Synthetic control/cue schedule;
// no original Windows playthrough or physical listening claim.
#define main vyruzApplicationMain
#include "../src/main.cpp"
#undef main
#include <fstream>
#include <stdexcept>
namespace vyruz {
struct AmmoImpactNativeTestAccess {static void money(GameWorld&w){w.audioEvent(AudioEventKind::MapCash,w.playerIndex());}};
struct AudioPipelineAuditAccess {
 static void freezeOutput(AudioDevice&d){SDL_PauseAudioDevice(d.device_,1);}
 static void render(AudioDevice&d,float*out,size_t n){d.mixer_.mix(out,n);}
};
}
static void need(bool b,const std::string&s){if(!b)throw std::runtime_error(s);}
static std::vector<uint8_t> saved(const App&a){vyruz::SaveGame s;s.currentLevel=a.content.mapPath;s.levels[s.currentLevel].world=a.world.snapshot();std::string e;std::vector<uint8_t>b;need(vyruz::encodeSave(s,b,e),e);return b;}
static void wav(const std::filesystem::path&p,const std::vector<float>&samples){
 std::ofstream o(p,std::ios::binary);auto u16=[&](unsigned n){o.put(n);o.put(n>>8);};auto u32=[&](unsigned n){u16(n);u16(n>>16);};
 o.write("RIFF",4);u32(36+samples.size()*2);o.write("WAVEfmt ",8);u32(16);u16(1);u16(2);u32(48000);u32(192000);u16(4);u16(16);o.write("data",4);u32(samples.size()*2);
 for(auto x:samples)u16(uint16_t(int16_t(std::clamp(int(std::lround(x*32768)),-32768,32767))));need(bool(o),"write capture");
}
int main(int argc,char**argv)try{
 need(argc==4,"usage: audio_pipeline_app_audit DATA MUSIC OUTPUT");std::filesystem::path out=argv[3];std::filesystem::create_directories(out);
 need(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_AUDIO)==0,SDL_GetError());std::ofstream report(out/"result.txt");std::vector<uint8_t> reference;
#if VYRUZ_GLES
 SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_ES);
 SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,3);SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,0);
#endif
 for(bool balanced:{false,true}){
  App a;std::string error;a.retryPersistence=false;a.optionsPath=(out/(balanced?"balanced.cfg":"legacy.cfg")).string();a.savePath=(out/"unused.vzs").string();a.scorePath=(out/"unused.sco").string();
  a.options.width=640;a.options.height=480;a.options.vsync=false;a.options.balancedAudio=balanced;a.options.qualityResampling=balanced;
  a.window=SDL_CreateWindow("Audio pipeline audit",0,0,640,480,SDL_WINDOW_OPENGL|SDL_WINDOW_HIDDEN);
  need(a.window&&a.gfx.init(a.window),SDL_GetError());need(a.vfs.init(argv[1]),a.vfs.error());need(a.audio.device().open(error),error);a.audio.device().setMasterVolume(.7f);
  need(applyOptionsRuntime(a,a.options,error,false),error);need(loadLevelData(a,"UNDERGROUND/T1U1.3DG"),a.loadError);
  vyruz::AudioPipelineAuditAccess::freezeOutput(a.audio.device());a.audio.reset();a.presentation.close();a.sessionStarted=true;a.menu.close(a.paused);
  a.audio.setListenerHalfWidth(320);a.world.setDebugGodMode(true);a.world.debugFillAmmo();
  bool gatling=false;for(unsigned slot=0;slot<a.world.player().weapons.size();++slot)if(vyruz::folded(a.world.player().weapons[slot].name).find("gatling")!=std::string::npos){need(a.world.selectPlayerWeaponSlot(slot),"select Gatling");gatling=true;break;}
  need(gatling,"resource player Gatling");need(a.audio.musicFile(argv[2],error),error);a.audio.reset();
  auto* splash=vyruz::findClass(a.content,"Water-Splash");auto* boom=vyruz::findClass(a.content,"Explosion");need(splash&&boom,"resource FX");auto splashId=splash->id,boomId=boom->id;
  const auto origin=a.world.playerPosition();std::vector<float>capture(48000*12*2);std::array<float,1600> paused{};
  for(unsigned tick=0;tick<720;++tick){
   if(tick>=120&&tick<540){
    if(tick%30==0)a.world.spawn(splashId,{origin.x-100,origin.y});
    if(tick%90==0)a.world.spawn(boomId,{origin.x+110,origin.y+10});
    if(tick%45==0)vyruz::AmmoImpactNativeTestAccess::money(a.world);
   }
   if(tick==300){a.audio.speech("V-KEY.WAV",0,0,0);a.audio.updateSpeech();}
   if(tick>=360&&tick<480)a.world.setPlayerPosition(origin.x+float(tick-360)*2,origin.y);
   a.world.tick(1.f/60,{{},tick>=120&&tick<540});advanceSceneAnimation(a,1000./60,a.world.elapsedMilliseconds());
   if(tick%60==0){const auto before=saved(a);consumeGameplayAudio(a);need(saved(a)==before,"audio changed persistent World");}
   else consumeGameplayAudio(a);
   a.audio.updateMusic();
   if(tick==400){const auto before=a.audio.device().musicState().milliseconds;a.audio.device().setPaused(true);vyruz::AudioPipelineAuditAccess::render(a.audio.device(),paused.data(),800);need(a.audio.device().musicState().milliseconds==before,"pause freezes music source clock");a.audio.device().setPaused(false);}
   vyruz::AudioPipelineAuditAccess::render(a.audio.device(),capture.data()+tick*1600,800);
   if(tick%60==0)render(a);
  }
  const auto end=saved(a);if(!balanced)reference=end;else need(reference==end,"mix profile changed simulation continuation");
  wav(out/(balanced?"balanced-scene.wav":"legacy-scene.wav"),capture);const auto stats=a.audio.device().statistics();need(stats.requests>20&&stats.clipped==0,"audible scene admission and bounded output");
  report<<(balanced?"balanced":"legacy")<<" requests="<<stats.requests<<" dropped="<<stats.rejected<<" repeats="<<stats.repetitions<<" underruns="<<stats.underruns<<" peak="<<stats.outputPeak<<" min_gain="<<stats.minimumGain<<" overload_frames="<<stats.overloads<<" input_peak="<<stats.inputPeak<<'\n';
  // New options use isolated persistence. Capture both common resolutions.
  a.menu.show(a.paused,true);a.menu.showOptions();a.optionsPage=9;a.menu.selected=0;refreshOptions(a);need(a.menu.optionLabels.size()==8,"advanced mix controls");
  SDL_FlushEvents(SDL_FIRSTEVENT,SDL_LASTEVENT);auto key=[&](SDL_Keycode k){SDL_Event e{};e.type=SDL_KEYDOWN;e.key.keysym.sym=k;SDL_PushEvent(&e);handleInput(a);};
  key(SDLK_RIGHT);need(a.options.balancedAudio!=balanced,"balanced control toggles");key(SDLK_LEFT);need(a.options.balancedAudio==balanced,"balanced control restores");
  const auto controls=a.options;
  for(int row=1;row<=5;++row){a.menu.selected=row;key(SDLK_RIGHT);need(a.options!=controls,"tuning control changes preference");key(SDLK_LEFT);need(a.options==controls,"tuning control roundtrip");}
  a.menu.selected=0;
  vyruz::GameOptions disk;need(vyruz::loadOptions(a.optionsPath,disk,error)&&disk.balancedAudio==balanced,"mix preferences persist");
  for(auto [width,height]:{std::pair{640,480}}){SDL_SetWindowSize(a.window,width,height);SDL_PumpEvents();a.gfx.resize(width,height);a.gfx.setSwapEnabled(true);render(a);render(a);a.gfx.setSwapEnabled(false);render(a);glFinish();need(saveScreenshot((out/(std::string(balanced?"balanced-":"legacy-")+std::to_string(width)+"-controls.ppm")).c_str(),width,height),"capture controls");a.gfx.setSwapEnabled(true);}
  a.presentation.close();a.menu.close(a.paused);need(loadLevelData(a,"CAVE/T1C1.3DG"),a.loadError);a.audio.device().setPaused(false);a.world.tick(1.f/60,{});advanceSceneAnimation(a,1000./60,a.world.elapsedMilliseconds());consumeGameplayAudio(a);vyruz::AudioPipelineAuditAccess::render(a.audio.device(),paused.data(),800);
  need(!a.renderFailed,"post-transition scene");report<<"world isolation, listener move, pause/resume, transition, controls PASS\n";
  a.audio.device().close();a.gfx.shutdown();SDL_DestroyWindow(a.window);
 }
 // Offscreen EGL drawables do not reliably resize: create a fresh wide context.
 for(bool balanced:{false,true}){
#if VYRUZ_GLES
  break; // Native handheld display checks use its actual640x480 drawable.
#endif
  App a;std::string error;a.options.width=1280;a.options.height=720;a.options.vsync=false;a.options.balancedAudio=balanced;
  a.retryPersistence=false;a.optionsPath=(out/"unused-wide.cfg").string();a.savePath=(out/"unused-wide.vzs").string();a.scorePath=(out/"unused-wide.sco").string();
  a.window=SDL_CreateWindow("Wide audio controls",0,0,1280,720,SDL_WINDOW_OPENGL|SDL_WINDOW_HIDDEN);
  need(a.window&&a.gfx.init(a.window),"wide GL context");need(a.vfs.init(argv[1]),a.vfs.error());need(loadLevelData(a,"UNDERGROUND/T1U1.3DG"),a.loadError);
  a.presentation.close();a.menu.show(a.paused,true);a.menu.showOptions();a.optionsPage=9;a.menu.selected=0;refreshOptions(a);a.gfx.setSwapEnabled(false);render(a);glFinish();
  need(saveScreenshot((out/(std::string(balanced?"balanced-":"legacy-")+"1280-controls.ppm")).c_str(),1280,720),"wide controls capture");
  a.audio.device().close();a.gfx.shutdown();SDL_DestroyWindow(a.window);
 }
 SDL_Quit();need(bool(report),"report write");return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}
