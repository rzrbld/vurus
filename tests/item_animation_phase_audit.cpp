#define main vyruzApplicationMain
#include "../src/main.cpp"
#undef main
#include "render/native_render_cells.h"
#include <stdexcept>
#include <iostream>
using namespace vyruz;
void check(bool b,const std::string&e){if(!b)throw std::runtime_error(e);}
bool equal(AnimationCursorState a,AnimationCursorState b){return a.cursor==b.cursor&&a.flags==b.flags&&a.remaining==b.remaining;}
int main(int argc,char**argv){try{
 check(argc==2,"usage: item_animation_phase_audit DATA");check(SDL_Init(SDL_INIT_VIDEO)==0,"SDL");App a;a.retryPersistence=false;a.window=SDL_CreateWindow("Item phases",0,0,640,480,SDL_WINDOW_OPENGL|SDL_WINDOW_HIDDEN);check(a.window&&a.gfx.init(a.window),"GL");check(a.vfs.init(argv[1]),"VFS");std::map<std::string,GafAnimation> banks;unsigned checks=0,outside=0,inside=0,audioPlays=0,births=0;
 for(auto map:{"CAVE/T1C2.3DG","UNDERGROUND/T2U1.3DG","HITEK/T1H1.3DG","HITEK/T2H2.3DG"}){
 check(loadLevelData(a,map,nullptr,false,false,false,1),a.loadError);a.presentation.close();a.menu.close(a.paused);a.paused=false;
 a.sceneRenderer.setNativeEnemyPhases();a.sceneRenderer.setNativeItemPhases(true);
 auto eligible=[&](const SceneChannelState& c){if(c.firing||c.object>=a.content.objects.size())return false;const auto&o=a.content.objects[c.object];const auto&cls=a.content.classes[o.classId.value];return objectSubtype(o)=="item"&&!propertyBool(cls.properties,"Graphic-update-needed");};
 auto sequence=[&](const SceneChannelState& c)->const GafSequence&{auto[it,added]=banks.try_emplace(c.bankPath);if(added){std::vector<uint8_t>b;check(a.vfs.read(c.bankPath,b)&&decodeGafAnimation(b,it->second,GafDecodeMode::Metadata),"GAF");}return it->second.sequences.at(c.sequence-1);};
 for(unsigned step=0;step<20;++step){
 auto before=a.sceneRenderer.capture();const double dt=step==19?5000:step%3==0?0:step%3==1?17:91;
 a.sceneRenderer.advance(dt,&a.world.actors(),dt*step,&a.world.surprise(),a.world.renderActorRevision());auto after=a.sceneRenderer.capture();
 for(size_t i=0;i<before.channels.size();++i)if(eligible(before.channels[i])){check(equal(before.channels[i].cursor,after.channels[i].cursor),"global Item advancement");++checks;}
 std::optional<size_t> born;
 if(step==7){for(const auto& c:before.channels)if(eligible(c)&&a.world.actors()[c.object].pickup){const auto& object=a.content.objects[c.object];born=a.world.spawn(object.classId,{1000,1000});break;}check(born.has_value(),"no Item birth fixture");std::string error;check(a.sceneRenderer.sync(a.content,error),error);++births;}
 auto view=enemyAnimationView(a,float((step%3)*1700),float((step%2)*1400));
 if(born){const auto position=a.content.objects[*born].position;view=enemyAnimationView(a,position.x-320,position.y-240);}
 if(step==19&&a.content.mapPath=="CAVE/T1C2.3DG"){const auto p=a.content.objects[68].position;view=enemyAnimationView(a,p.x-320,p.y-240);}view.playerOnly=step%5==0;
 if(step%3==1)for(const auto& c:a.sceneRenderer.capture().channels)if(propertyBool(a.content.classes[a.content.objects[c.object].classId.value].properties,"Saving-plot")){view.savingPlot=c.object;break;}
 const auto window=nativeRenderWindow(view.translationX,view.translationY,view.width,view.height,view.mapW,view.mapH);
 for(auto phase:{EnemyAnimationPhase::VisibleUpdate,EnemyAnimationPhase::Draw}){
 a.sceneRenderer.takeAudioCues();std::vector<std::tuple<size_t,std::string,uint32_t>> expectedPlays;before=a.sceneRenderer.capture();a.sceneRenderer.groundDetails=step%2==0;a.sceneRenderer.advanceEnemyPhase(dt,a.world.actors(),view,phase);after=a.sceneRenderer.capture();
 for(size_t i=0;i<before.channels.size();++i)if(eligible(before.channels[i])){
 const auto& c=before.channels[i];AnimationCursor expected;check(expected.restore(sequence(c),c.cursor),"restore cursor");
 auto cell=a.world.nativeActorCells().cell[c.object];bool admitted=false;if(cell!=NativeActorCells::Missing){int x=int(cell/size_t(view.mapH)),y=int(cell%size_t(view.mapH));admitted=x>=window.left&&x<window.right&&y>=window.top&&y<window.bottom;}
 admitted=admitted&&!view.playerOnly&&a.world.actors()[c.object].alive;
 if(!c.shadow&&propertyBool(a.content.classes[a.content.objects[c.object].classId.value].properties,"Saving-plot")&&view.savingPlot!=c.object)admitted=false;
 if(admitted){expected.advance(sequence(c),dt,false,[&](const auto& frame){if(!c.shadow&&(frame.command&1)&&!(frame.command&0x10000)&&frame.sample)expectedPlays.emplace_back(c.object,c.bankPath,frame.sample);});++inside;}else ++outside;
 check(equal(expected.capture(),after.channels[i].cursor),"Item phase cursor mismatch "+std::string(map)+" object "+std::to_string(c.object));++checks;
 }
 std::vector<std::tuple<size_t,std::string,uint32_t>> actualPlays;for(const auto& cue:a.sceneRenderer.takeAudioCues())if(cue.kind==SceneAudioCueKind::Play&&cue.object<a.content.objects.size()){const auto&o=a.content.objects[cue.object];if(objectSubtype(o)=="item"&&!propertyBool(a.content.classes[o.classId.value].properties,"Graphic-update-needed")&&!o.flags.invisible.value_or(false))actualPlays.emplace_back(cue.object,cue.bankPath,cue.sample);}
 std::sort(actualPlays.begin(),actualPlays.end());std::sort(expectedPlays.begin(),expectedPlays.end());check(actualPlays==expectedPlays,"Item audio playback mismatch");audioPlays+=actualPlays.size();
 }
 if(step==8){auto saved=a.sceneRenderer.capture();std::string e;check(a.sceneRenderer.restore(saved,e),e);}
 }
 }
 check(inside&&outside&&audioPlays,"missing camera/audio controls");std::cout<<"checks="<<checks<<" admitted="<<inside<<" excluded="<<outside<<" Item_births="<<births<<" Item_audio_plays="<<audioPlays<<" mismatches=0\n";return 0;
 }catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
