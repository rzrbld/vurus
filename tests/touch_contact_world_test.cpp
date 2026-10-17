#include "game/world.h"
#include "game/properties.h"
#include "game/save_file.h"
#include "game/save_state.h"
#include <iostream>
#include <stdexcept>
using namespace vyruz;
static TextNode value(std::string key,std::string val){TextNode n;n.kind=TextKind::Assignment;n.key=std::move(key);n.values={std::move(val)};return n;}
static TextNode block(std::string type,std::vector<TextNode> fields){TextNode n;n.kind=TextKind::Block;n.key="TYPE";n.arguments={std::move(type)};n.children=std::move(fields);return n;}
static TextNode group(std::string key,std::vector<TextNode> fields){auto n=block("",std::move(fields));n.key=std::move(key);n.arguments.clear();return n;}
static ContentPosition surface(float x,float z){const auto p=spiderNativeToSurface(x,z);return {p.x,p.y};}
namespace vyruz {struct AmmoImpactNativeTestAccess {
 static void setup(GameWorld&w,bool movingPlayer,bool rigid){
  w.elapsedMs_=100;w.currentTickMilliseconds_=14;w.surprise_.kind=SurpriseKind::Touch;w.surprise_.remainingMs=10000;w.surprise_.applied=true;
  for(size_t n=0;n<3;n++){auto&a=w.actors_[n];a.ageMs=1000;a.ammoContactDisabled=false;a.ammoHidden=false;a.indestructible=n==1&&rigid;a.contactVelocity={};a.enemyMagnitude=20;a.enemyMoveCommand=200;a.contactResponseMs=n==1?23:27;a.contactMotion=false;a.nativeHit=false;}
  const size_t source=movingPlayer?0:1,target=movingPlayer?1:0;
  w.scene_->objects[source].position=surface(1500,500);w.scene_->objects[target].position=surface(1533,500);
  w.scene_->objects[2].position=movingPlayer?surface(1534,500):surface(2000,500);
  for(size_t n=0;n<3;n++){auto p=w.scene_->objects[n].position;auto c=spiderSurfaceToNative(p.x,p.y);w.actors_[n].nativeMotion=SpiderMotionHistory{{c.x,c.y},surface(c.x-10,c.y)};}
  w.actors_[source].contactVelocity={20,0};w.motion_.velocityX=20;w.motion_.velocityZ=0;w.motion_.magnitude=20;
  w.initializeActorCells();w.initializeSpiderNavigation(true);
 }
 static NativeBodyContactQuery query(GameWorld&w,bool movingPlayer){return w.queryNativeBodyContacts(movingPlayer?0:1,{1500,500},{1520,500},8);}
 static bool exactTouchTail(GameWorld&w,bool movingPlayer){size_t source=movingPlayer?0:1;auto p=surface(1500,500);return w.actors_[1].alive&&w.actors_[1].nativeDamagePending&&!w.actors_[1].nativeHit&&w.actors_[1].contactMotion&&w.actors_[1].contactResponseMs==23&&w.actors_[0].contactResponseMs==27&&w.contactDamage_.empty()&&w.touchVictims_.empty()&&w.scene_->objects[source].position.x==p.x&&w.scene_->objects[source].position.y==p.y;}
 static void normalizeDiskContinuation(GameWorld&w){w.cancelSurprise();}
 static void setupCachedImpact(GameWorld&w,bool movingPlayer){w.cancelSurprise();const size_t source=movingPlayer?0:1;w.actors_[source].nativeAmmoContinuation=true;w.actors_[source].enemyMagnitude=3;w.motion_.magnitude=3;}
 static bool cachedImpact(GameWorld&w,bool movingPlayer){const size_t source=movingPlayer?0:1;const float expected=3*w.actors_[source].contactConfig.weight*.5f;if(w.contactDamage_.size()!=(movingPlayer?1u:2u))return false;for(const auto&r:w.contactDamage_)if(r.value!=expected)return false;return true;}
 static void move(GameWorld&w,bool movingPlayer){ContactVelocity step{20,0};auto d=nativeToSurface(step.x,step.z);w.move(movingPlayer?0:1,d.x,d.y,true,&step);}
};}
static std::vector<uint8_t> bytes(GameWorld&w){SaveGame save;save.currentLevel="TEST/TOUCH.3DG";save.levels[save.currentLevel].world=w.snapshot();std::vector<uint8_t>b;std::string e;if(!encodeSave(save,b,e))throw std::runtime_error(e);return b;}
static SceneContent fixtureScene(){ SceneContent s;s.mapPath="TEST/TOUCH.3DG";
 for(unsigned n=0;n<3;n++){ObjectClass c;c.id={n};c.name=n==0?"Player":n==1?"Cop":"Ammo";c.behaviours={n==0?"Player":"Fixed"};c.properties={value("Sub-type",n==0?"Vyruz":n==1?"Cop":"Ammo"),value("Managed","1"),value("Bounding-box-size","8"),group("Energy",{value("Initial","1000"),value("Max","1000")})};if(n==2)c.properties.push_back(block("Ammo",{value("Damage","1"),value("Damage-zone","1"),value("Ignore-owner","0")}));s.classes.push_back(c);ObjectPlacement o;o.id={n};o.classId={n};o.properties=c.properties;o.behaviours=c.behaviours;o.position=surface(1000+n*100,500);s.objects.push_back(o);}
 s.player=ObjectId{0};return s;}
int main(){unsigned failed=0;for(bool movingPlayer:{false,true})for(bool rigid:{false,true}){
 SceneContent s=fixtureScene();Level l;l.mapW=l.mapH=128; s.player=ObjectId{0};const auto authored=s;GameWorld w;std::string e;if(!w.load(s,l,e))throw std::runtime_error(e);AmmoImpactNativeTestAccess::setup(w,movingPlayer,rigid);auto q=AmmoImpactNativeTestAccess::query(w,movingPlayer);std::cerr<<"query used="<<q.used<<" selected="<<(q.selected?std::to_string(q.selected->reference):"none")<<" ammo="<<q.hasAmmoCandidate<<" ignored="<<q.callerIgnored<<" movingPlayer="<<movingPlayer<<" rigid="<<rigid<<"\n";size_t expectedTarget=movingPlayer?1:0;if(!q.used||!q.selected||q.selected->reference!=expectedTarget||(movingPlayer&&!q.hasAmmoCandidate))throw std::runtime_error("invalid supplied common-query fixture");AmmoImpactNativeTestAccess::move(w,movingPlayer);float actual=w.actors()[1].vitals.energy,expected=rigid?1000:0;std::cout<<"movingPlayer="<<movingPlayer<<" indestructibleCop="<<rigid<<" selected="<<q.selected->reference<<" hasAmmoCandidate="<<q.hasAmmoCandidate<<" copEnergy="<<actual<<" expected="<<expected<<'\n';failed+=actual!=expected;
 if(!rigid&&actual==expected){if(!AmmoImpactNativeTestAccess::exactTouchTail(w,movingPlayer))throw std::runtime_error("Touch deferred death/rollback/latch/no timestamp/no hit/no damage ownership");auto encoded=bytes(w);SaveGame decoded;if(!decodeSave(encoded,decoded,e))throw std::runtime_error(e);if(decoded.levels.begin()->second.world.surprise.kind!=SurpriseKind::None||!decoded.levels.begin()->second.world.actors[1].nativeDamagePending)throw std::runtime_error("disk clears Touch while retaining its pending death");
 // Normal disk saves omit active Surprises. Normalize the live baseline too;
 // otherwise it can deliver another Touch during the pending victim's next visit.
 AmmoImpactNativeTestAccess::normalizeDiskContinuation(w);auto resumedScene=authored;GameWorld resumed;if(!resumed.load(resumedScene,l,e)||!resumed.restoreSnapshot(decoded.levels.begin()->second.world,e))throw std::runtime_error(e);if(bytes(w)!=bytes(resumed))throw std::runtime_error("pending Touch disk roundtrip");for(unsigned tick=0;tick<40;tick++){w.tick(.014f,{});resumed.tick(.014f,{});if(bytes(w)!=bytes(resumed))throw std::runtime_error("pending Touch death disk continuation");}if(w.actors()[1].alive)throw std::runtime_error("Touch victim never reaches own death visit");}
 }
 for(bool movingPlayer:{false,true}){auto s=fixtureScene();Level l;l.mapW=l.mapH=128;GameWorld w;std::string e;if(!w.load(s,l,e))throw std::runtime_error(e);AmmoImpactNativeTestAccess::setup(w,movingPlayer,false);AmmoImpactNativeTestAccess::setupCachedImpact(w,movingPlayer);auto q=AmmoImpactNativeTestAccess::query(w,movingPlayer);if(!q.selected||q.selected->reference!=(movingPlayer?1u:0u)||(movingPlayer&&!q.hasAmmoCandidate))throw std::runtime_error("cached impact fixture winner");AmmoImpactNativeTestAccess::move(w,movingPlayer);if(!AmmoImpactNativeTestAccess::cachedImpact(w,movingPlayer))throw std::runtime_error("shared impact must read retained magnitude, not provisional velocity");std::cout<<"cachedImpact movingPlayer="<<movingPlayer<<" PASS\n";}
 std::cout<<"failures="<<failed<<'\n';return failed?1:0;}
