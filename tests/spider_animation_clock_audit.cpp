#define main vyruzApplicationMain
#include "../src/main.cpp"
#undef main
#include "game/save_state.h"
#include <iostream>
#include <stdexcept>
using namespace vyruz;
static void need(bool ok,const std::string& error){if(!ok)throw std::runtime_error(error);}
static size_t spiderId=SIZE_MAX;
static SceneChannelState body(const App& a){for(const auto& c:a.sceneRenderer.capture().channels)if(c.object==spiderId&&!c.shadow&&!c.firing)return c;throw std::runtime_error("Spider body missing");}
static bool same(AnimationCursorState a,AnimationCursorState b){return a.cursor==b.cursor&&a.flags==b.flags&&a.remaining==b.remaining;}
int main(int argc,char**argv){try{
 need(argc==3,"usage: spider_animation_clock_audit DATA OUTPUT");std::filesystem::create_directories(argv[2]);
 need(SDL_Init(SDL_INIT_VIDEO)==0,"SDL");App a;a.window=SDL_CreateWindow("Spider clock",0,0,640,480,SDL_WINDOW_OPENGL|SDL_WINDOW_HIDDEN);
 need(a.window&&a.gfx.init(a.window),"GL");a.gfx.setSwapEnabled(false);need(a.vfs.init(argv[1]),a.vfs.error());
 ContentReader read=[&](const auto&p,auto&b){return a.vfs.read(p,b);};std::string error;
 need(loadLevelData(a,"UNDERGROUND/T1U1.3DG",nullptr,false,false,true,1),"load original map graphics");
 for(size_t id=0;id<a.content.objects.size();++id)if(objectSubtype(a.content.objects[id])=="spider"){spiderId=id;break;}
 need(spiderId!=SIZE_MAX,"authored Spider");const auto location=a.content.objects[spiderId].position;
 a.world.setPlayerPosition(location.x-130,location.y);a.worldX=location.x-130;a.worldY=location.y;
 a.haveLevel=a.sessionStarted=true;a.options.shadows=a.options.reactorFx=a.options.groundDetails=false;
 auto state=a.world.snapshot();state.actors[spiderId].direction=6;state.actors[spiderId].walkingCommand=true;state.actors[spiderId].enemyMoveCommand=200;
 need(a.world.restoreSnapshot(state,error),error);need(a.world.actors()[spiderId].walkingCommand,"restore walking command");
 EnemyAnimationView view;view.unboundedOracle=true;
 a.sceneRenderer.advanceEnemyPhase(17,a.world.actors(),view,EnemyAnimationPhase::VisibleUpdate);a.enemyDrawStepMs=17;
 render(a);const auto first=body(a).cursor;need(first.cursor==1&&first.remaining==22,"first native frame:56-17-17ms");
 need(saveScreenshot((std::filesystem::path(argv[2])/"first.ppm").c_str(),a.gfx.drawableWidth(),a.gfx.drawableHeight()),"first image");
 render(a);const auto second=body(a).cursor;render(a);const auto third=body(a).cursor;
 std::cout<<"draw-only frames: "<<first.cursor<<'/'<<first.remaining<<" -> "<<second.cursor<<'/'<<second.remaining<<" -> "<<third.cursor<<'/'<<third.remaining<<'\n';
 need(saveScreenshot((std::filesystem::path(argv[2])/"third.ppm").c_str(),a.gfx.drawableWidth(),a.gfx.drawableHeight()),"third image");
 need(same(first,second)&&same(first,third),"redraw without a new simulation step must not replay elapsed animation time");
 // A real subsequent17ms update and17ms draw must still advance normal gait.
 a.sceneRenderer.advanceEnemyPhase(17,a.world.actors(),view,EnemyAnimationPhase::VisibleUpdate);a.enemyDrawStepMs+=17;render(a);
 need(body(a).cursor.cursor==2&&body(a).cursor.remaining==44,"continuous walking retains native56ms frame duration");
 //43f07f–43fc42: catch-up has one visible phase per slice, then draws
 //with only the last slice interval, not their sum.
 a.enemyDrawStepMs=0;advanceSceneAnimation(a,17,17);advanceSceneAnimation(a,17,34);
 need(a.enemyDrawStepMs==17,"catch-up draw uses last native slice interval");render(a);need(a.enemyDrawStepMs==0,"draw consumes pending interval once");
 need(body(a).cursor.cursor==3&&body(a).cursor.remaining==49,"two visible slices and last-slice draw preserve native gait clock");
 std::cout<<"PASS application draw clock, original walking threshold and catch-up interval\n";
 a.sceneRenderer.clear(a.gfx);a.hud.clear(a.gfx);a.gfx.shutdown();SDL_DestroyWindow(a.window);SDL_Quit();
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
