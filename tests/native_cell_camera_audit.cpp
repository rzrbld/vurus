#define main vyruzApplicationMain
#include "../src/main.cpp"
#undef main
#include "game/save_file.h"
#include "render/native_render_cells.h"
#include <fstream>
#include <stdexcept>
using namespace vyruz;
void check(bool ok,const std::string& error){if(!ok)throw std::runtime_error(error);}
std::vector<uint8_t> snap(App&a,bool renderer){SaveGame s;s.currentLevel=a.content.mapPath;s.levels[s.currentLevel].world=a.world.snapshot();if(renderer)s.levels[s.currentLevel].render=a.sceneRenderer.capture();std::vector<uint8_t>b;std::string e;check(encodeSave(s,b,e),e);return b;}
std::vector<uint8_t> pixels(){std::vector<uint8_t>b(640*480*3);glPixelStorei(GL_PACK_ALIGNMENT,1);glReadPixels(0,0,640,480,GL_RGB,GL_UNSIGNED_BYTE,b.data());check(glGetError()==GL_NO_ERROR,"pixels");return b;}
int main(int argc,char**argv){try{
 check(argc==3||argc==4,"usage: native_cell_camera_audit DATA OUTPUT [TICKS]");const unsigned ticks=argc==4?std::stoul(argv[3]):180;std::filesystem::path out=argv[2];std::filesystem::create_directories(out);check(SDL_Init(SDL_INIT_VIDEO)==0,"SDL");
 App a,b;SDL_GLContext contexts[2];App* apps[]={&a,&b};
 for(unsigned i=0;i<2;++i){auto&x=*apps[i];x.retryPersistence=false;x.window=SDL_CreateWindow("Native cells",0,0,640,480,SDL_WINDOW_OPENGL|SDL_WINDOW_HIDDEN);check(x.window&&x.gfx.init(x.window),"GL");contexts[i]=SDL_GL_GetCurrentContext();x.gfx.setSwapEnabled(false);check(x.vfs.init(argv[1]),"VFS");}
 std::ofstream log(out/"results.csv");log<<"map,tick,pixel_differences,world_equal,render_state_equal\n";unsigned frames=0,diffFrames=0,stateDiff=0;
 struct Scene{const char*map;float x,y;};
 const Scene scenes[]={{"CAVE/T1C2.3DG",5230,1080},{"UNDERGROUND/T2U1.3DG",4870,2180},{"HITEK/T1H1.3DG",3670,2570},{"HITEK/T2H2.3DG",4571,2841}};
 for(const auto&spec:scenes){
  for(unsigned i=0;i<2;++i){auto&x=*apps[i];SDL_GL_MakeCurrent(x.window,contexts[i]);check(loadLevelData(x,spec.map,nullptr,false,false,false,1),x.loadError);x.presentation.close();x.menu.close(x.paused);x.paused=false;x.world.setDebugGodMode(true);x.world.debugFillAmmo();x.world.setPlayerPosition(spec.x,spec.y);x.sceneRenderer.setNativeDrawCells(i==1);}
  NativeRenderWindow previousWindow{};bool haveWindow=false;unsigned edgeCaptures=0;
  for(unsigned tick=0;tick<ticks;++tick){std::vector<uint8_t> pic[2];std::vector<SceneAudioCue> cues[2];
   for(unsigned i=0;i<2;++i){auto&x=*apps[i];SDL_GL_MakeCurrent(x.window,contexts[i]);x.previousPositions.clear();for(const auto&o:x.content.objects)x.previousPositions.push_back(o.position);WorldInput input;input.fire=tick<120&&std::string(spec.map)!="HITEK/T2H2.3DG";x.world.setPlayerDirection((tick/30)%24);
    // Camera-edge probe only: teleport the player equally in both worlds,
    // including fractional coordinates straddling64x32 cell boundaries.
    const float dx=float(int(tick%128)-64)+.25f;
    const float dy=float(int((tick*3)%96)-48)+.5f;
    x.world.setPlayerPosition(spec.x+dx,spec.y+dy);
    bindEffectCamera(x);x.world.tick(1.f/60,input);advanceSceneAnimation(x,1000./60,x.world.elapsedMilliseconds());check(!x.renderFailed,"animation");for(auto[id,rev]:x.sceneRenderer.finishedGates(x.world.actors()))x.world.finishGateAnimation(id,rev);for(auto id:x.sceneRenderer.finishedEffects(x.world.actors()))x.world.expireVisual(id);auto pos=x.world.playerPosition();x.worldX=pos.x;x.worldY=pos.y;x.motion.direction=x.world.player().direction;x.options.groundDetails=(tick/60)%2==0;render(x,1);pic[i]=pixels();x.world.takeAudioEvents();cues[i]=x.sceneRenderer.takeAudioCues();}
   const auto audio=[](const std::vector<SceneAudioCue>& cues){std::vector<std::tuple<size_t,std::string,uint32_t,int,uint64_t,std::optional<std::array<float,3>>>> r;for(const auto& c:cues)r.emplace_back(c.object,c.bankPath,c.sample,int(c.kind),c.animation,c.source);return r;};
   check(audio(cues[0])==audio(cues[1]),"Audio cues diverged "+std::string(spec.map)+" tick"+std::to_string(tick));
   const bool equal=snap(a,false)==snap(b,false);check(equal,"World diverged "+std::string(spec.map)+" tick"+std::to_string(tick));size_t changed=0;for(size_t n=0;n<pic[0].size();n+=3)changed+=pic[0][n]!=pic[1][n]||pic[0][n+1]!=pic[1][n+1]||pic[0][n+2]!=pic[1][n+2];bool re=snap(a,true)==snap(b,true);check(re,"Renderer state diverged "+std::string(spec.map)+" tick"+std::to_string(tick));diffFrames+=changed>0;stateDiff+=!re;++frames;log<<spec.map<<','<<tick<<','<<changed<<','<<equal<<','<<re<<'\n';
   const auto window=nativeRenderWindow(static_cast<int32_t>(-a.camX),static_cast<int32_t>(-a.camY),640,480,a.level.mapW,a.level.mapH);
   const bool edge=!haveWindow||window.left!=previousWindow.left||window.top!=previousWindow.top;
   previousWindow=window;haveWindow=true;
   if(tick+1==ticks||(edge&&edgeCaptures++<10)){for(unsigned i=0;i<2;++i){auto&x=*apps[i];SDL_GL_MakeCurrent(x.window,contexts[i]);check(saveScreenshot((out/(std::string(i?"native-":"baseline-")+std::to_string(frames)+".ppm")).c_str(),640,480),"capture");}}
   if(tick==90||tick==154){
    for(unsigned i=0;i<2;++i){auto&x=*apps[i];SDL_GL_MakeCurrent(x.window,contexts[i]);SaveGame saved,decoded;saved.currentLevel=x.content.mapPath;saved.levels[saved.currentLevel]={x.world.snapshot(),x.sceneRenderer.capture()};std::string e;auto path=(out/("checkpoint-"+std::to_string(i)+".vzs")).string();check(writeSaveFile(path,saved,e)&&readSaveFile(path,decoded,e),e);const auto& level=decoded.levels.at(saved.currentLevel);check(x.world.restoreSnapshot(level.world,e),e);check(x.sceneRenderer.restore(level.render,e),e);}
    check(snap(a,false)==snap(b,false),"World differs after file reload");
   }
  }
 }
 std::cout<<"frames="<<frames<<" pixel_diff_frames="<<diffFrames<<" renderer_state_diff_frames="<<stateDiff<<" World byte-equal throughout\n";
 return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
