// Port-only debug presentation/input integration. Original resources stay private.
#define main vyruzApplicationMain
#include "../src/main.cpp"
#undef main
#include <fstream>
#include <stdexcept>
static void need(bool ok,const std::string& why){if(!ok)throw std::runtime_error(why);}
static std::vector<uint8_t> savedWorld(const App& a){
 vyruz::SaveGame save;save.currentLevel=levelKey(a.content.mapPath);save.levels[save.currentLevel].world=a.world.snapshot();
 std::string error;std::vector<uint8_t> bytes;need(vyruz::encodeSave(save,bytes,error),error);return bytes;
}
int main(int argc,char**argv){try{
 need(argc==3,"usage: debug_commander_audit DATA OUTPUT");const std::filesystem::path out=argv[2];std::filesystem::create_directories(out);
 need(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_EVENTS)==0,SDL_GetError());std::ofstream report(out/"result.txt");
 for(int width:{640,1280}){
  const int height=width==640?480:720;App a;
  a.optionsPath=(out/(std::to_string(width)+"-options.cfg")).string();a.savePath=(out/"unused-save.vzs").string();a.scorePath=(out/"unused-score.sco").string();
  a.window=SDL_CreateWindow("Debug commander audit",0,0,width,height,SDL_WINDOW_OPENGL|SDL_WINDOW_HIDDEN);
  need(a.window&&a.gfx.init(a.window),SDL_GetError());need(a.vfs.init(argv[1]),a.vfs.error());need(loadLevelData(a,"CAVE/T1C1.3DG"),a.loadError);
  a.presentation.close();a.sessionStarted=true;SDL_FlushEvents(SDL_FIRSTEVENT,SDL_LASTEVENT);
  glDrawBuffer(GL_FRONT_AND_BACK);glReadBuffer(GL_FRONT);
  const auto key=[&](SDL_Keycode code){SDL_Event e{};e.type=SDL_KEYDOWN;e.key.keysym.sym=code;need(SDL_PushEvent(&e)==1,"push key");handleInput(a);applyMenuAction(a);};
  const auto mouse=[&](float x,float y,bool click){const auto v=a.gfx.viewport();SDL_Event e{};e.type=click?SDL_MOUSEBUTTONDOWN:SDL_MOUSEMOTION;
   const int px=int((x+v.uiX())*width/v.width),py=int((y+v.uiY())*height/v.height);
   if(click){e.button.button=SDL_BUTTON_LEFT;e.button.x=px;e.button.y=py;}else{e.motion.x=px;e.motion.y=py;}
   need(SDL_PushEvent(&e)==1,"push mouse");handleInput(a);applyMenuAction(a);
  };
  const auto shot=[&](const char* name){render(a);need(saveScreenshot((out/(std::to_string(width)+"-"+name+".ppm")).c_str(),width,height),"capture");};
  key(SDLK_F2);need(!a.menu.open(),"release F2 is inert");
  a.menuAction=vyruz::MenuAction::DebugInfo;applyMenuAction(a);need(a.debugInfoVisible,"release debug info action rejected");
  a.debugEnabled=true;a.menu.debugEnabled=true;key(SDLK_F2);
  need(a.menu.page==vyruz::MenuPage::Debug&&a.menu.debugPreview().size()==25&&a.paused,"F2 opens actions and level preview");shot("levels-preview");
  key(SDLK_TAB);need(a.menu.debugListFocused(),"Tab focuses right list");key(SDLK_END);shot("levels-scroll");
  need(a.menu.selected==24&&a.menu.firstRow()==15,"last level visible in continuous list");
  key(SDLK_PAGEUP);need(a.menu.selected==14,"PageUp moves ten entries");key(SDLK_LEFT);need(a.menu.page==vyruz::MenuPage::Debug&&a.menu.selected==0,"Left focuses action owner");
  key(SDLK_RIGHT);need(a.menu.selected==14&&a.menu.debugListFocused(),"Right restores child selection");key(SDLK_ESCAPE);
  a.menu.selected=5;key(SDLK_RIGHT);key(SDLK_END);shot("surprises-scroll");need(a.menu.selected==20&&a.menu.firstRow()==11,"all Surprises reachable");key(SDLK_ESCAPE);
  const bool ai=a.world.debugEnemyAI();mouse(vyruz::MenuView::DebugLeft+8,vyruz::MenuView::DebugTop+30-10,true);
  need(a.menu.page==vyruz::MenuPage::Debug&&a.menu.selected==1&&a.world.debugEnemyAI()!=ai,"left action is clickable from right list");
  mouse(vyruz::MenuView::DebugLeft+8,vyruz::MenuView::DebugTop-10,false);
  mouse(vyruz::MenuView::DebugRight+8,vyruz::MenuView::DebugTop+26*2-10,false);
  need(a.menu.page==vyruz::MenuPage::DebugLevels,"mouse focuses preview list");
  SDL_Event wheel{};wheel.type=SDL_MOUSEWHEEL;wheel.wheel.y=-3;need(SDL_PushEvent(&wheel)==1,"wheel");handleInput(a);
  need(a.menu.selected>=3,"mouse wheel scrolls child list");
  const auto before=savedWorld(a);
  mouse(vyruz::MenuView::DebugLeft+8,vyruz::MenuView::DebugTop+6*30-10,true);
  need(!a.debugInfoVisible&&a.menu.page==vyruz::MenuPage::Debug,"main debug info toggle works from right pane");shot("info-off");
  need(before==savedWorld(a),"info toggle and debug presentation preserve encoded World");
  key(SDLK_ESCAPE);key(SDLK_ESCAPE);need(!a.menu.open()&&!a.paused,"Back resumes play");
  a.options.fpsCounter=false;a.debugMemoryNext=UINT64_MAX;
  render(a);render(a);
  const auto pixels=[&](){glFinish();std::vector<uint8_t> data(size_t(width)*height*3);glReadPixels(0,0,width,height,GL_RGB,GL_UNSIGNED_BYTE,data.data());need(glGetError()==GL_NO_ERROR,"pixel readback");return data;};
  shot("game-info-off");const auto hidden=pixels();
  a.menuAction=vyruz::MenuAction::DebugInfo;applyMenuAction(a);need(a.debugInfoVisible,"info can be enabled again");shot("game-info-on");const auto shown=pixels();
  size_t changes=0;for(size_t p=0;p<shown.size();p+=3)if(!std::equal(shown.begin()+p,shown.begin()+p+3,hidden.begin()+p))++changes;
  need(changes>100,"toggle changes actual diagnostic pixels");
  a.menuAction=vyruz::MenuAction::DebugInfo;applyMenuAction(a);key(SDLK_F2);need(a.menu.debugScreen()&&!a.debugInfoVisible,"F2 remains available with info hidden");
  report<<"PASS "<<width<<"x"<<height<<": release gate, preview, Tab/arrows, continuous scroll, PageUp, retained selection, mouse/wheel, info toggle, World isolation, real overlay pixels="<<changes<<"\n";
  a.loading.clear(a.gfx);a.sceneRenderer.clear(a.gfx);a.hud.clear(a.gfx);a.gfx.shutdown();SDL_DestroyWindow(a.window);
 }
 SDL_Quit();return 0;
}catch(const std::exception&e){std::fprintf(stderr,"debug commander audit: %s\n",e.what());return 1;}}
