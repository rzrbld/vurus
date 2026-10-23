#include "game/world.h"
#include "game/save_file.h"
#include "game/properties.h"
#include "snapshot_fixture.h"
#include <iostream>
#include <stdexcept>
using namespace vyruz;
TextNode v(std::string k,std::string s){TextNode n;n.kind=TextKind::Assignment;n.key=k;n.values={s};return n;}
TextNode b(std::string k,std::vector<TextNode> c,std::string t=""){TextNode n;n.kind=TextKind::Block;n.key=k;n.children=c;if(!t.empty())n.arguments={t};return n;}
static void checkCase(bool item,bool point,bool visual,bool ignored){SceneContent s;s.mapPath="TEST/COLLISION.3DG";ObjectClass player;player.id={0};player.name="Player";player.behaviours={"Player"};player.properties={v("Managed","1"),v("Sub-type","Vyruz"),b("Energy",{v("Initial","1000")})};
ObjectClass t;t.id={1};t.name="Target";t.behaviours={"Fixed"};t.properties={v("Managed","1"),v("Sub-type","Cop"),v("Bounding-box-size","8"),b("Energy",{v("Initial","1000")})};ObjectClass t2=t;t2.id={2};t2.name="Target2";t2.properties[2]=v("Bounding-box-size",point?"13":"12");if(item){t.properties[1]=v("Sub-type","Item");t.properties.push_back(b("TYPE",{v("Only-for-player","0")},"Item"));}ObjectClass ammo;ammo.id={3};ammo.name="Laser-W-Ammo";ammo.behaviours={"Straight"};ammo.properties={v("Managed","1"),v("Sub-type","Ammo"),b("TYPE",{v("Damage","100"),v("Damage-zone","0")},"Ammo")};
if(ignored){
    t.properties[1]=v("Sub-type","Item");t2.properties[1]=v("Sub-type","Item");
    t.properties.push_back(b("TYPE",{v("Only-for-player",point?"1":"0")},"Item"));
    t2.properties.push_back(b("TYPE",{v("Only-for-player",point?"0":"1")},"Item"));
}
ammo.properties.push_back(v("Bounding-box-size",point?"1":"10"));
s.classes={player,t,t2,ammo};
for(unsigned i=0;i<3;++i){ObjectPlacement o;o.id={i};o.classId={i};o.properties=s.classes[i].properties;o.behaviours=s.classes[i].behaviours;auto p=spiderNativeToSurface(i?1000+(i==1?23:27):500,i?1000:500);o.position={p.x,p.y};s.objects.push_back(o);}s.player=ObjectId{0};Level l;l.mapW=l.mapH=100;l.hasTerrainCollision=true;GameWorld w;std::string e;if(!w.load(s,l,e)){throw std::runtime_error(e);}const auto initialScene=s;Projectile p;p.classId={3};p.owner=0;auto pos=spiderNativeToSurface(1000,1000);p.position={pos.x,pos.y};p.radius=point?1:10;p.damage=100;p.damageRadius=0;p.ageMs=1000;p.lifetimeMs=10000;p.directionX=1;p.directionZ=0;p.motionConfig.inertia=true;p.motionConfig.launched=true;p.motionConfig.frictionX=p.motionConfig.frictionZ=1;p.motion.velocityX=20;p.motion.magnitude=20;if(visual)p.visual=w.spawn({3},p.position);auto state=w.snapshot();vyruz_test::appendProjectile(state,s,p);if(!w.restoreSnapshot(state,e)){throw std::runtime_error(e);}
SaveGame checkpoint,decoded;checkpoint.currentLevel=s.mapPath;checkpoint.levels[s.mapPath].world=w.snapshot();
std::vector<uint8_t> bytes; if(!encodeSave(checkpoint,bytes,e)||!decodeSave(bytes,decoded,e))throw std::runtime_error(e);
auto resumedScene=initialScene;GameWorld resumed;
if(!resumed.load(resumedScene,l,e)||!resumed.restoreSnapshot(decoded.levels.at(s.mapPath).world,e))throw std::runtime_error(e);
w.tick(.014f,{});resumed.tick(.014f,{});
const size_t winner=point&&!item?1:2,other=winner==1?2:1;

if(ignored){
    if(w.actors()[1].vitals.energy!=1000 || w.actors()[2].vitals.energy!=1000 || w.projectiles().size()!=1)
        throw std::runtime_error("Only-for-player winner is ignored without selecting another target");
}else if(w.actors()[winner].vitals.energy!=900 || w.actors()[other].vitals.energy!=1000 || !w.projectiles().empty())
    throw std::runtime_error("World native projectile contact winner");
for(unsigned tick=0;tick<10;++tick){
    SaveGame a,b;a.currentLevel=b.currentLevel=s.mapPath;
    a.levels[s.mapPath].world=w.snapshot();b.levels[s.mapPath].world=resumed.snapshot();
    std::vector<uint8_t> ab,bb;
    if(!encodeSave(a,ab,e)||!encodeSave(b,bb,e)||ab!=bb)throw std::runtime_error("native contact disk continuation differs");
    w.tick(.014f,{});resumed.tick(.014f,{});
}
}
// Native decor exercises the production graph path, unlike surface-only
// collisionEdges used by the older terrain-response fixtures.
static void checkTerrain(bool point,bool visual,bool bounce,bool water,bool overWater,bool rectangle){
    SceneContent s;s.mapPath="TEST/NATIVE-TERRAIN.3DG";
    ObjectClass player;player.id={0};player.name="Player";player.behaviours={"Player"};
    player.properties={v("Managed","1"),b("Energy",{v("Initial","1000")})};
    ObjectClass ammo;ammo.id={1};ammo.name="Test-Ammo";ammo.behaviours={"Straight"};
    ammo.properties={v("Managed","1"),v("Sub-type","Ammo"),v("Bounding-box-size",point?"1":"10"),
        v("Elasticity","0.6"),v("Over-water",overWater?"1":"0"),
        b("TYPE",{v("On-hit",bounce?"0":"1"),v("On-object-hit","1")},"Ammo")};
    s.classes={player,ammo};ObjectPlacement o;o.id={0};o.classId={0};o.position={200,200};
    o.properties=player.properties;o.behaviours=player.behaviours;s.objects={o};s.player=ObjectId{0};
    const auto initial=s;Level l;l.mapW=l.mapH=100;l.hasTerrainCollision=true;
    l.nativeNavigationDecor.push_back({1015,rectangle?990.f:900.f,rectangle?1030.f:1015.f,rectangle?1010.f:1100.f,0,0,1,water,false,rectangle});
    GameWorld w;std::string e;if(!w.load(s,l,e))throw std::runtime_error(e);
    Projectile p;p.classId={1};p.owner=0;const auto pos=spiderNativeToSurface(1000,1000);p.position={pos.x,pos.y};
    p.radius=point?1:10;p.ageMs=1000;p.lifetimeMs=10000;p.directionX=1;p.directionZ=0;
    p.motionConfig.inertia=true;p.motionConfig.launched=true;p.motionConfig.frictionX=p.motionConfig.frictionZ=1;
    p.motion.thrust=20;p.motion.velocityX=20;p.motion.magnitude=20;
    if(visual)p.visual=w.spawn({1},p.position);
    auto state=w.snapshot();if(visual)state.actors[*p.visual].direction=6;
    vyruz_test::appendProjectile(state,s,p);if(!w.restoreSnapshot(state,e))throw std::runtime_error(e);
    SaveGame save,decoded;save.currentLevel=s.mapPath;save.levels[s.mapPath].world=w.snapshot();std::vector<uint8_t> bytes;
    if(!encodeSave(save,bytes,e)||!decodeSave(bytes,decoded,e))throw std::runtime_error(e);
    auto rs=initial;GameWorld resumed;
    if(!resumed.load(rs,l,e)||!resumed.restoreSnapshot(decoded.levels.at(s.mapPath).world,e))throw std::runtime_error(e);
    w.tick(.014f,{});resumed.tick(.014f,{});
    const bool ignored=water&&overWater;
    if(w.projectiles().empty()!=( !bounce&&!ignored))throw std::runtime_error("native terrain terminal/rebound/Over-water policy");
    if(bounce&&!ignored && w.projectiles()[0].motion.velocityX>=0)throw std::runtime_error("native wall response reverses approach point="+std::to_string(point)+" visual="+std::to_string(visual)+" water="+std::to_string(water)+" rectangle="+std::to_string(rectangle)+" vx="+std::to_string(w.projectiles()[0].motion.velocityX));
    for(unsigned tick=0;tick<10;++tick){
        SaveGame a,b;a.currentLevel=b.currentLevel=s.mapPath;a.levels[s.mapPath].world=w.snapshot();b.levels[s.mapPath].world=resumed.snapshot();
        std::vector<uint8_t> ab,bb;if(!encodeSave(a,ab,e)||!encodeSave(b,bb,e)||ab!=bb)throw std::runtime_error("native terrain disk continuation");
        w.tick(.014f,{});resumed.tick(.014f,{});
    }
}
static void checkBoundary(bool visual,unsigned edge){
    SceneContent s;s.mapPath="TEST/AMMO-BOUNDARY.3DG";
    ObjectClass player;player.id={0};player.name="Player";player.behaviours={"Player"};
    player.properties={v("Managed","1"),b("Energy",{v("Initial","1000")})};
    ObjectClass ammo;ammo.id={1};ammo.name="Boundary-Ammo";ammo.behaviours={"Straight"};
    ammo.properties={v("Managed","1"),v("Sub-type","Ammo"),v("Bounding-box-size","10"),b("TYPE",{v("Damage","100")},"Ammo")};
    s.classes={player,ammo};ObjectPlacement o;o.id={0};o.classId={0};o.position={200,200};o.properties=player.properties;o.behaviours=player.behaviours;s.objects={o};s.player=ObjectId{0};
    Level l;l.mapW=l.mapH=100;l.hasTerrainCollision=true;GameWorld w;std::string e;
    if(!w.load(s,l,e))throw std::runtime_error(e);
    const ContentPosition positions[]={{2,1000},{6398,1000},{1000,2},{1000,3198}};
    const ContactVelocity velocities[]={{-20,20},{20,-20},{-20,-20},{20,20}};
    Projectile p;p.classId={1};p.owner=0;p.position=positions[edge];p.radius=10;p.ageMs=1000;p.lifetimeMs=10000;
    p.motionConfig.inertia=true;p.motionConfig.launched=true;p.motionConfig.frictionX=p.motionConfig.frictionZ=1;
    p.motion.velocityX=velocities[edge].x;p.motion.velocityZ=velocities[edge].z;p.motion.magnitude=std::hypot(p.motion.velocityX,p.motion.velocityZ);
    if(visual)p.visual=w.spawn({1},p.position);
    auto state=w.snapshot();vyruz_test::appendProjectile(state,s,p);
    if(!w.restoreSnapshot(state,e))throw std::runtime_error(e);
    w.tick(.014f,{});
    if(!w.projectiles().empty() || (visual && w.actors()[*p.visual].alive) || w.actors()[0].vitals.energy!=1000)
        throw std::runtime_error("moving Ammo map-boundary release before contacts");
}
int main(){try{for(bool visual:{false,true})for(unsigned edge=0;edge<4;++edge)checkBoundary(visual,edge);for(bool item:{false,true})for(bool point:{false,true})for(bool visual:{false,true})checkCase(item,point,visual,false);for(bool point:{false,true})for(bool visual:{false,true})checkCase(false,point,visual,true);for(bool point:{false,true})for(bool visual:{false,true})for(bool bounce:{false,true})for(bool water:{false,true})for(bool over:{false,true})for(bool rect:{false,true})checkTerrain(point,visual,bounce,water,over,rect);std::cout<<"World native projectile contacts: circle/point, bare/visual, Item priority, ignored winner and disk continuation passed\n";return 0;}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
