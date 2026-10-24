// Original-resource App menu audit. Native contract is static PE evidence;
// captures/input below exercise the port, not a Windows playthrough.
#define main vyruzApplicationMain
#include "../src/main.cpp"
#undef main
#include <fstream>
#include <stdexcept>
namespace vyruz {struct AudioPipelineAuditAccess {static void freeze(AudioDevice&d){SDL_PauseAudioDevice(d.device_,1);}};}
static void need(bool b,const std::string&s){if(!b)throw std::runtime_error(s);}
static std::vector<uint8_t> snapshot(const App&a){vyruz::SaveGame s;s.currentLevel=a.content.mapPath;s.levels[s.currentLevel].world=a.world.snapshot();std::vector<uint8_t>b;std::string e;need(vyruz::encodeSave(s,b,e),e);return b;}
int main(int argc,char**argv)try{
 need(argc==3,"usage: audio_menu_native_audit DATA OUTPUT");const std::filesystem::path out=argv[2];std::filesystem::create_directories(out);
 need(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_AUDIO)==0,SDL_GetError());std::ofstream report(out/"result.txt");
#if VYRUZ_GLES
 SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_ES);
 SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,3);SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,0);
 for(int width:{640}){
#else
 for(int width:{640,1280}){
#endif
  const int height=width==640?480:720;App a;std::string error;
  a.options.width=width;a.options.height=height;a.options.vsync=false;a.retryPersistence=false;
  a.optionsPath=(out/("options-"+std::to_string(width)+".cfg")).string();a.savePath=(out/"unused.vzs").string();a.scorePath=(out/"unused.sco").string();
  a.window=SDL_CreateWindow("Native audio menus",0,0,width,height,SDL_WINDOW_OPENGL|SDL_WINDOW_HIDDEN);need(a.window&&a.gfx.init(a.window),"GL context");need(a.vfs.init(argv[1]),a.vfs.error());
  need(a.audio.device().open(error),error);need(applyOptionsRuntime(a,a.options,error,false),error);need(loadLevelData(a,"UNDERGROUND/T1U1.3DG"),a.loadError);
  vyruz::AudioPipelineAuditAccess::freeze(a.audio.device());a.presentation.close();a.menu.show(a.paused,true);a.menu.showOptions();a.optionsPage=2;refreshOptions(a);const auto world=snapshot(a);
  const auto key=[&](SDL_Keycode code){SDL_Event e{};e.type=SDL_KEYDOWN;e.key.keysym.sym=code;need(optionsEvent(a,e),"options key");};
  const auto choose=[&](int row){a.menu.selected=row;key(SDLK_RETURN);};
  const auto capture=[&](const char*name){a.gfx.setSwapEnabled(false);render(a);glFinish();need(saveScreenshot((out/(std::to_string(width)+"-"+name+".ppm")).c_str(),width,height),"capture");a.gfx.setSwapEnabled(true);};
  need(a.menu.optionLabels==std::vector<std::string>({"Music","Sound FX, Voices","Quality : 32 bits","Frequency : 48000 Hz","Channels : Stereo","Menu sounds: On","Advanced","Back"}),"native root ordering with supported modern output");capture("audio");choose(5);need(!a.options.menuSounds,"root menu sounds switch");choose(5);need(a.options.menuSounds,"root menu sounds restored");
  choose(0);need(a.optionsPage==12&&a.menu.optionLabels[1]=="Tune Music Volume","Music page");capture("music");
  choose(0);need(!a.options.music,"music off");choose(1);need(a.optionsPage==12&&a.menu.selectedHelp()=="Music is desactivated.","disabled music tune gated");
  choose(0);need(a.options.music,"music on");choose(1);need(a.optionsPage==17&&a.menu.optionVolume==1,"music volume visible");
  key(SDLK_LEFT);need(a.options.musicVolume==.99f,"volume1percent left");a.menuHold.reset();
  SDL_Event wheel{};wheel.type=SDL_MOUSEWHEEL;wheel.wheel.y=-4;optionsEvent(a,wheel);need(a.options.musicVolume==.95f,"volume wheel");
  const auto view=a.gfx.viewport();SDL_Event click{};click.type=SDL_MOUSEBUTTONDOWN;click.button.button=SDL_BUTTON_LEFT;
  click.button.x=int((vyruz::MenuView::VolumeLeft+.37f*(vyruz::MenuView::VolumeRight-vyruz::MenuView::VolumeLeft)+view.uiX())*width/view.width);
  click.button.y=int((180+view.uiY())*height/view.height);optionsEvent(a,click);need(std::abs(a.options.musicVolume-.37f)<.011f,"volume bar click");capture("music-volume");
  key(SDLK_RETURN);need(a.optionsPage==12&&a.menu.selected==1,"volume Back parent");key(SDLK_ESCAPE);need(a.optionsPage==2&&a.menu.selected==0,"Music Escape parent");
  choose(1);need(a.optionsPage==10&&a.menu.optionLabels.size()==6,"combined FX Voices");capture("fx-voices");
  choose(0);choose(1);need(!a.options.soundFx&&a.optionsPage==10&&a.menu.selectedHelp()=="Sound FX are removed.","FX disabled tune gate");choose(0);
  choose(1);need(a.optionsPage==18,"FX volume page");key(SDLK_LEFT);a.menuHold.reset();need(a.options.effectsVolume==.99f,"FX independent volume");capture("fx-volume");key(SDLK_ESCAPE);need(a.optionsPage==10&&a.menu.selected==1,"FX volume parent");
  choose(2);choose(3);need(!a.options.voices&&a.optionsPage==10,"voices disabled tune gate");choose(4);need(a.optionsPage==10,"voice type disabled gate");choose(2);
  choose(3);need(a.optionsPage==19,"voice volume page");key(SDLK_LEFT);a.menuHold.reset();need(a.options.voicesVolume==.99f&&a.options.effectsVolume==.99f,"voice independent volume");capture("voice-volume");key(SDLK_ESCAPE);
  choose(4);need(a.optionsPage==11,"voice type choices");capture("voice-type");const auto musicSerial=a.audio.device().musicState().serial;
  choose(1);need(a.optionsPage==10&&a.options.voiceLanguage==2&&a.audio.voiceLanguage()=="FRENCH","French voice pack applies");need(a.audio.device().musicState().serial==musicSerial,"voice choice does not reset music");
  key(SDLK_ESCAPE);need(a.optionsPage==2&&a.menu.selected==1,"FX Voices parent");choose(6);need(a.optionsPage==9,"Advanced remains isolated");capture("advanced");need(a.menu.optionLabels.size()==8,"compact advanced rows keep help at main Audio height");
  for(int row=0;row<6;++row){a.menu.selected=row;const auto name="advanced-help-"+std::to_string(row);capture(name.c_str());}
  key(SDLK_ESCAPE);need(a.optionsPage==2&&a.menu.selected==6,"Advanced parent");
  vyruz::GameOptions loaded;need(vyruz::loadOptions(a.optionsPath,loaded,error),error);need(loaded.voiceLanguage==2&&loaded.effectsVolume==.99f&&loaded.voicesVolume==.99f&&std::abs(loaded.musicVolume-.37f)<.011f,"volume/type persistence");
  need(snapshot(a)==world,"audio controls preserve World");
  need(loadLevelData(a,"CAVE/T1C1.3DG"),a.loadError);need(a.audio.voiceLanguage()=="FRENCH","voice type survives map load");
  report<<"PASS "<<width<<"x"<<height<<": native hierarchy, Music/FX/Voices gates, visible20segment volumes, keyboard/wheel/mouse, parent navigation, voice pack/persistence, World isolation, map continuation; advanced mixer retained\n";
  a.audio.device().close();a.gfx.shutdown();SDL_DestroyWindow(a.window);
 }
 SDL_Quit();return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}
