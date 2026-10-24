// Controlled PCM, original-resource App transitions; not a Windows listening test.
#define main vyruzApplicationMain
#include "../src/main.cpp"
#undef main
#include <stdexcept>
static void need(bool value,const std::string& why){if(!value)throw std::runtime_error(why);}
static std::vector<uint8_t> longWave(){
 std::vector<uint8_t> b(44+48000*5,160);
 for(auto [p,s]:{std::pair{0,"RIFF"},{8,"WAVE"},{12,"fmt "},{36,"data"}})std::copy_n(s,4,b.begin()+p);
 auto n=[&](size_t p,uint32_t v){for(int i=0;i<4;++i)b[p+i]=uint8_t(v>>(8*i));};
 n(4,uint32_t(b.size()-8));n(16,16);n(20,0x00010001);n(24,48000);n(28,48000);n(32,0x00080001);n(40,uint32_t(b.size()-44));return b;
}
int main(int argc,char** argv)try{
 need(argc==3,"usage: transition_audio_audit DATA OUTPUT");const std::filesystem::path out=argv[2];std::filesystem::create_directories(out);
 need(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_AUDIO)==0,SDL_GetError());App a;
#if VYRUZ_GLES
 SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_ES);
 SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,3);SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,0);
#endif
 a.window=SDL_CreateWindow("Transition audio",0,0,640,480,SDL_WINDOW_OPENGL|SDL_WINDOW_HIDDEN);
 need(a.window&&a.gfx.init(a.window),SDL_GetError());need(a.vfs.init(argv[1]),a.vfs.error());std::string error;
 a.retryPersistence=false;a.savePath=(out/"unused.vzs").string();a.optionsPath=(out/"options.cfg").string();a.scorePath=(out/"scores").string();
 need(a.audio.device().open(error),error);
 // Extend only the two tested clips so slow CI rendering cannot make an
 // admitted voice expire before the post-load assertion. Playback routing and
 // all level/World/GAF resources remain real; no audio-clock sleeps needed.
 need(a.audio.load({},[&](const auto& path,auto& bytes){
  const auto name=vyruz::folded(path);
  if(name.ends_with("v-well-done.wav")||name.ends_with("fade.wav")){bytes=longWave();return true;}
  return a.vfs.read(path,bytes);
 },"ENGLISH",error),error);
 need(loadLevelData(a,"CAVE/T1C1.3DG"),a.loadError);a.presentation.close();a.sessionStarted=true;a.menu.close(a.paused);a.audio.reset();
 SDL_FlushEvents(SDL_FIRSTEVENT,SDL_LASTEVENT);
 auto key=[&](SDL_Keycode symbol,SDL_Scancode scan){SDL_Event e{};e.type=SDL_KEYDOWN;e.key.keysym.sym=symbol;e.key.keysym.scancode=scan;SDL_PushEvent(&e);handleInput(a);};
 const auto capture=[&](const char* filename){
  // EGL can exchange/discard the backbuffer on swap. Match --shot: capture
  // this completed render before swap, then display it on the device.
  a.gfx.setSwapEnabled(false);render(a);glFinish();
  need(saveScreenshot((out/filename).c_str(),640,480),"pre-swap UI capture");
  a.gfx.setSwapEnabled(true);a.gfx.present();
 };
 need(beginExtra(a,vyruz::MenuAction::ControlsHelp),"Controls Help opens");
 need(a.audio.device().nativeChannelPlaying(5)&&a.helpRows.size()>20,"Controls HELP starts despite modal speech suspension");
 finishPresentation(a);a.menu.close(a.paused);a.presentation.close();a.audio.reset();SDL_Delay(1050);
 // Live Panel Help: edge-triggered, remappable, enable-only speech; no pause.
 const auto elapsed=a.world.elapsedMilliseconds();key(SDLK_F3,SDL_SCANCODE_F3);a.audio.updateSpeech();
 need(a.panelHelp&&!a.paused&&a.world.elapsedMilliseconds()>elapsed,"Panel Help remains live");
 need(a.audio.device().nativeChannelPlaying(5),"enabling Panel Help queues HELP");
 capture("panel-help-en.ppm");
 a.hud.setLanguage(2);capture("panel-help-fr.ppm");a.hud.setLanguage(1);
 SDL_Event repeat{};repeat.type=SDL_KEYDOWN;repeat.key.keysym.sym=SDLK_F3;repeat.key.keysym.scancode=SDL_SCANCODE_F3;repeat.key.repeat=1;SDL_PushEvent(&repeat);handleInput(a);need(a.panelHelp,"held repeat cannot toggle help");
 a.audio.device().stopNativeChannels(1u<<4);key(SDLK_F3,SDL_SCANCODE_F3);a.audio.updateSpeech();need(!a.panelHelp&&!a.audio.device().nativeChannelPlaying(5),"disabling Panel Help has no voice");
 a.options.keyBindings[size_t(vyruz::OptionAction::PanelHelp)]=SDL_SCANCODE_F4;
 key(SDLK_F3,SDL_SCANCODE_F3);need(!a.panelHelp,"old Panel Help binding stays inactive");key(SDLK_F4,SDL_SCANCODE_F4);need(a.panelHelp,"remapped Panel Help works");a.panelHelp=false;
 // The HELP hold gate is native-global and intentionally survives resets.
 SDL_Delay(1050);a.audio.reset();
 key(SDLK_PAGEDOWN,SDL_SCANCODE_PAGEDOWN);a.audio.consume(a.world,a.content,{});a.audio.updateSpeech();
 need(a.world.snapshot().nativePlayerSpeed&&a.world.playerSpeedIndicatorLow(),"Page Down admits native low mode");
 need(a.audio.device().nativeChannelPlaying(5),"low-speed commentary reaches voice channel");
 const auto speed=a.world.snapshot().nativePlayerSpeed->speedThreshold;
 repeat.key.keysym.sym=SDLK_PAGEDOWN;repeat.key.keysym.scancode=SDL_SCANCODE_PAGEDOWN;SDL_PushEvent(&repeat);handleInput(a);
 need(a.world.snapshot().nativePlayerSpeed->speedThreshold==speed,"held Page Down does not retoggle");
 key(SDLK_PAGEDOWN,SDL_SCANCODE_PAGEDOWN);need(!a.world.playerSpeedIndicatorLow(),"second press restores high speed");
 a.options.keyBindings[size_t(vyruz::OptionAction::SpeedMode)]=SDL_SCANCODE_F5;
 key(SDLK_PAGEDOWN,SDL_SCANCODE_PAGEDOWN);need(!a.world.playerSpeedIndicatorLow(),"old speed binding inactive");
 key(SDLK_F5,SDL_SCANCODE_F5);need(a.world.playerSpeedIndicatorLow(),"remapped speed binding works");
 key(SDLK_F5,SDL_SCANCODE_F5);a.audio.reset();
 // Actual App disk reader/writer must retain manually selected low mode.
 need(a.world.togglePlayerSpeedMode(),"low before disk save");a.saveHistory=false;a.savePath=(out/"speed-mode.vzs").string();
 need(saveSession(a,0,error),error);need(loadSession(a),a.loadError);a.presentation.close();
 need(a.world.snapshot().nativePlayerSpeed&&a.world.playerSpeedIndicatorLow(),"manual speed survives App disk reload");
 need(a.world.snapshot().nativePlayerFacing->periodMs==27,"App disk reload retains native turn period");
 a.options.menuSounds=false;a.menu.show(a.paused,false);key(SDLK_ESCAPE,SDL_SCANCODE_ESCAPE);
 need(!a.menu.open()&&a.audio.device().nativeChannelPlaying(1),"game-menu resume plays FADE even with selection clicks off");
 a.audio.reset();a.paused=false;key(SDLK_p,SDL_SCANCODE_P);
 need(a.paused&&!a.audio.device().nativeChannelPlaying(1),"ordinary pause does not play FADE");a.paused=false;
 a.debugEnabled=true;a.menu.showDebug(a.paused);
 capture("debug-commander.ppm");
 key(SDLK_ESCAPE,SDL_SCANCODE_ESCAPE);
 need(a.menu.open()&&!a.audio.device().nativeChannelPlaying(1),"debug navigation does not play FADE");a.menu.close(a.paused);
 SDL_Delay(1050);
 constexpr size_t lift=164;need(a.world.actors().at(lift).lift.has_value(),"authored Cave exit Lift");
 const auto before=a.world.snapshot();a.audio.reset();
 need(!changeLevel(a,{lift,"CAVE/MISSING-AUDIO-AUDIT.3DG",0}),"missing destination rejects");
 need(!a.audio.device().nativeChannelPlaying(5)&&!a.audio.device().nativeChannelPlaying(1),"failed Lift has neither departure speech nor FADE");
 need(a.world.restoreSnapshot(before,error),error);
 need(changeLevel(a,{lift,"CAVE/T1C2.3DG",0}),a.loadError);
 need(a.audio.device().nativeChannelPlaying(5),"WELL-DONE survives real destination and mission/audio resets");
 need(a.world.snapshot().nativePlayerSpeed&&a.world.snapshot().nativePlayerSpeed->speedThreshold==15,"default-zero Lift Destination retains manual mode");
 need(!a.audio.device().nativeChannelPlaying(1),"ordinary Lift does not play terminal FADE");
 a.audio.reset();need(!a.audio.device().nativeChannelPlaying(5),"departure preservation scope ends after commit");
 // Later-map Player placements rely on carried vitals. Enter through the
 // previous authored Lift instead of constructing an uninitialized campaign body.
 need(changeLevel(a,{667,"HITEK/T1H1.3DG",0}),a.loadError);a.presentation.close();
 need(a.world.player().vitals.energy>0,"living carried player before retained-body branch");
 need(a.world.actors().at(738).lift.has_value(),"authored retained-player Lift");
 need(changeLevel(a,{738,"HITEK/T1H2.3DG",0}),a.loadError);
 need(a.world.snapshot().nativePlayerSpeed&&a.world.snapshot().nativePlayerSpeed->speedThreshold==15&&a.world.snapshot().nativePlayerFacing->periodMs==27,"retained-player Lift preserves native manual fields");

 // Native hold deadline is one second; the next independent transition must
 // respect it. Wait only in this test, outside gameplay and profiling.
 SDL_Delay(1050);a.presentation.close();
 need(changeLevel(a,{SIZE_MAX,"*",0}),a.loadError);
 need(a.presentation.open()&&a.audio.device().nativeChannelPlaying(1),"terminal sentinel plays FADE after presentation reset");
 need(!a.audio.device().nativeChannelPlaying(5),"non-Lift ending has no WELL-DONE producer");
 a.audio.reset();
 size_t endingLift=0;while(endingLift<a.world.actors().size()&&!a.world.actors()[endingLift].lift)++endingLift;
 need(endingLift<a.world.actors().size(),"authored ending control Lift");
 need(changeLevel(a,{endingLift,"*",0}),a.loadError);
 need(a.audio.device().nativeChannelPlaying(5)&&a.audio.device().nativeChannelPlaying(1),"Lift ending preserves WELL-DONE alongside terminal FADE");
 std::ofstream(out/"result.txt")<<"PASS resume/ordinary-pause/debug gates; rejected Lift silence; admitted Lift voice through real map+mission reset; ordinary Lift no FADE; exact scope cleanup; terminal FADE. Extended controlled PCM, original levels, controlled requests; no Windows listening/campaign playthrough.\n";
 a.audio.device().close();a.loading.clear(a.gfx);a.sceneRenderer.clear(a.gfx);a.hud.clear(a.gfx);a.gfx.shutdown();SDL_DestroyWindow(a.window);SDL_Quit();return 0;
}catch(const std::exception& e){std::fprintf(stderr,"transition audio audit: %s\n",e.what());return 1;}
