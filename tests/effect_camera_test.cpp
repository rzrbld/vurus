#include "game/world.h"
#include "snapshot_fixture.h"
#include "game/save_file.h"
#include <algorithm>
#include <array>
#include <climits>
#include <cstdlib>
#include <iostream>
using namespace vyruz;
namespace {
using Bytes=std::vector<uint8_t>;
void need(bool b,const char* why){if(!b){std::cerr<<why<<'\n';std::exit(1);}}
void put(Bytes& b,size_t o,uint32_t v){for(int i=0;i<4;++i)b[o+i]=v>>(8*i);}
void append(Bytes& a,const Bytes& b){a.insert(a.end(),b.begin(),b.end());}
Bytes chunk(const char* tag,Bytes data,uint32_t children=0){Bytes b(16);for(int i=0;i<4;++i)b[i]=tag[i];put(b,4,data.size());put(b,12,children);append(b,data);return b;}
Bytes streams(const std::array<Bytes,4>& parts){Bytes b(96);for(size_t i=0;i<4;++i){put(b,i*24+16,parts[i].size());append(b,parts[i]);while(b.size()%4)b.push_back(0);}return b;}
// Eight active poses: the delayed payload fires on the midpoint, not removal.
Bytes animation(){
 Bytes descriptor(44);put(descriptor,0,1);put(descriptor,4,1);put(descriptor,8,1);
 Bytes bitmap(72);bitmap[0]='B';bitmap[1]='M';bitmap[2]='P';bitmap[3]='_';put(bitmap,4,4);put(bitmap,8,0x40000322);put(bitmap,12,56);put(bitmap,16,0x40000322);put(bitmap,20,1);put(bitmap,24,16);put(bitmap,28,16);put(bitmap,32,1);put(bitmap,36,1);put(bitmap,40,4);bitmap[69]=0x7c;
 Bytes seq(32);put(seq,0,8);put(seq,8,1);Bytes poses(8*28);for(int i=0;i<8;++i){put(poses,i*28,1);put(poses,i*28+24,2);}
 Bytes hg(36),ha(36);put(hg,0,1);put(ha,0,1);Bytes head;append(head,chunk("GOB_",hg));append(head,chunk("ANM_",ha));Bytes data;append(data,chunk("GOB_",streams({descriptor,bitmap,{},{}})));append(data,chunk("ANM_",streams({seq,{},poses,{}})));Bytes root;append(root,chunk("HEAD",head,2));append(root,chunk("DATA",data,2));return chunk("GAF_",root,2);
}
TextNode value(std::string k,std::string v){TextNode n;n.kind=TextKind::Assignment;n.key=k;n.values={v};return n;}
TextNode block(std::string k,std::vector<TextNode> c,std::string arg={}){TextNode n;n.kind=TextKind::Block;n.key=k;n.children=c;if(!arg.empty())n.arguments={arg};return n;}
void place(SceneContent& s,unsigned c,ContentPosition p){ObjectPlacement o;o.id={unsigned(s.objects.size())};o.classId={c};o.position=p;o.properties=s.classes[c].properties;o.behaviours=s.classes[c].behaviours;o.sprite=s.classes[c].sprite;s.objects.push_back(o);}
SceneContent scene(ContentPosition p={500,500}){
 SceneContent s;s.mapPath="TEST/CAMERA.3DG";
 for(const auto* name:{"Player","Source","Target","Boom","Drop","Ammo","Impact","Bullet-Ground-Impact"}){ObjectClass c;c.id={unsigned(s.classes.size())};c.name=name;c.behaviours={c.id.value?"Fixed":"Player"};s.classes.push_back(c);}
 s.classes[0].properties={block("Energy",{value("Initial","10000")})};
 s.classes[1].properties={block("Energy",{value("Initial","1")}),block("TYPE",{value("Damage","1000"),value("Damage-zone","100"),value("Boom-fx","Boom")},"Explosion")};
 TextNode have;have.kind=TextKind::Bare;have.key="HAVE";have.arguments={"Drop"};s.classes[1].properties.push_back(have);
 s.classes[2].properties={block("Energy",{value("Initial","10000")}),value("Bounding-box-size","10")};
 for(unsigned c:{3u,6u,7u})s.classes[c].properties={block("TYPE",{},"FX")};
 s.classes[3].sprite={"BOOM.GAF",{1}};
 // The point source must have an authored BBOX, including its visual body.
 s.classes[5].properties={value("Sub-type","Ammo"),value("Bounding-box-size","1"),value("Graphic-update-needed","1"),block("TYPE",{value("Hit-fx","Impact")},"Ammo"),block("TYPE",{value("Damage","1000"),value("Damage-zone","100"),value("Boom-fx","Boom")},"Explosion")};
 for(auto& cls:s.classes)cls.properties.push_back(value("Managed","1"));
 place(s,0,{100,100});place(s,1,p);place(s,2,p);s.player=ObjectId{0};return s;
}
Level level(){Level l;l.mapW=l.mapH=100;l.surfaceWidth=6336;l.surfaceHeight=3168;return l;}
void load(GameWorld& w,SceneContent& s,const Level& l){std::string e;w.setEffectReader([](const std::string&,Bytes& b){b=animation();return true;});need(w.load(s,l,e),e.c_str());w.setDebugEnemyAI(false);}
size_t count(const GameWorld& w,const SceneContent& s,unsigned cls){size_t n=0;for(size_t i=0;i<w.actors().size();++i)if(w.actors()[i].alive&&s.objects[i].classId.value==cls)++n;return n;}
void helper(){
 NativeEffectCamera c;
 for(int x:{-640,1279})for(int y:{-480,959})need(c.admits(x,y),"ordinary inclusive lower / last integer upper");
 for(auto p:{ContentPosition{-641,0},{1280,0},{0,-481},{0,960}})need(!c.admits(p.x,p.y),"ordinary outside rectangle");
 c.offsetX=37;c.offsetY=-19;need(c.admits(-677,-461)&&!c.admits(-678,-461),"offset applied before rectangle comparison");
 for(uint32_t mode:{0x8000u,0x10000u,0x18000u}){c={};c.modeFlags=mode;c.viewWidth=641;c.viewHeight=481;need(c.admits(-64,-64)&&c.admits(704,544),"tight mode boundaries and odd display extents");need(!c.admits(-65,0)&&!c.admits(705,0)&&!c.admits(0,545),"tight exclusive upper and outside lower");}
 c={};c.contextFlags=0x800;c.modeFlags=0x18000;c.surfaceWidth=101;c.surfaceHeight=51;c.playerX=200;c.playerY=300;c.viewWidth=c.viewHeight=1;c.offsetX=c.offsetY=100000;
 need(c.admits(150,275)&&c.admits(250,325),"context800 uses inclusive player +/- floor(surface/2), ignoring display/mode/offset");need(!c.admits(149,300)&&!c.admits(251,300)&&!c.admits(200,326),"context800 odd extent outside bounds");
 c.playerX=200.75f;c.playerY=300.25f;need(!c.admits(150,300)&&c.admits(151,276)&&c.admits(250,325)&&!c.admits(200,275),"context800 preserves fractional player position");
 c={};c.offsetX=INT32_MAX;need(c.admits(INT32_MIN+1,0),"native signed32 addition wraps before comparison");
}
void death(){
 const auto l=level();
 for(int flag=0;flag<5;++flag){
  auto s=scene();if(flag==2)s.classes[1].properties.push_back(value("Graphic-update-needed","1"));
  if(flag==3)s.objects[1].properties.push_back(value("Graphic-update-needed","1"));
  if(flag==4)s.classes[3].properties.push_back(value("Graphic-update-needed","1"));
  GameWorld w;load(w,s,l);NativeEffectCamera c;if(flag!=1)c.offsetX=-2000;w.setEffectCamera(c);w.damage(1,1,0);
  const bool admitted=flag==1||flag==2;need(count(w,s,3)==unsigned(admitted),"death admission bypass belongs only to source class");need(!w.actors()[1].alive&&count(w,s,4)==1,"offscreen source removal and HAVE drops continue");
  need(w.actors()[2].vitals.energy==10000,"death payload does not damage immediately");for(int i=0;i<40;++i)w.tick(1.f/60,{});need(w.actors()[2].vitals.energy==(admitted?9000:10000),"culled death has no delayed damage; admitted death applies it once");
 }
 for(float x:{-640.9f,-641.f}){auto s=scene({x,500});GameWorld w;load(w,s,l);w.setEffectCamera({});w.damage(1,1,0);need(count(w,s,3)==unsigned(x>-641),"death camera conversion truncates toward zero");}
 for(float x:{3268.f,3269.f}){auto s=scene({x,500});GameWorld w;load(w,s,l);NativeEffectCamera c;c.contextFlags=0x800;c.surfaceWidth=c.surfaceHeight=1;c.playerX=c.playerY=-10000;c.offsetX=-100000;c.viewWidth=1;w.setEffectCamera(c);w.damage(1,1,0);need(count(w,s,3)==unsigned(x==3268),"world context800 uses live player and exact level extent, not stale binding fields or padded map size");}
 // Legacy headless callers intentionally retain their camera-independent tests.
 auto s=scene({5000,2000});GameWorld w;load(w,s,l);w.damage(1,1,0);need(count(w,s,3)==1,"unset camera preserves headless mechanics");
}
void projectile(ContentPosition p,NativeEffectCamera c,bool admitted,bool contact=false){
 auto s=scene();s.objects.resize(1);if(contact)place(s,2,{500,500});const auto l=level();GameWorld w;load(w,s,l);w.setEffectCamera(c);
 Projectile shot;shot.classId={5};shot.owner=0;shot.ignoreOwner=true;shot.position=p;shot.hitEffect="Impact";shot.phaseInitialized=true;shot.motionConfig.inertia=true;shot.radius=1;
 if(contact){shot.position={450,500};shot.motion.velocityX=100;shot.motion.velocityZ=-100;shot.slopeAltitude=24;}else{shot.fuseMs=50;shot.damageRemainingMs=0;}
 shot.visual=w.spawn({5},shot.position);auto snap=w.snapshot();snap.actors[*shot.visual].solid=false;snap.actors[*shot.visual].projectileAltitude=contact?24:0;vyruz_test::appendProjectile(snap,s,shot);snap.spiderNavigation.reset();std::string e;need(w.restoreSnapshot(snap,e),e.c_str());w.tick(1.f/60,{});
 need(w.projectiles().empty(),"test shot terminates");need(count(w,s,3)==1,"Ammo source class bypasses explosion camera gate");need(count(w,s,contact?6:7)==unsigned(admitted),"direct hit FX always uses camera gate even for bypass Ammo");
 if(admitted&&!contact){for(size_t i=0;i<s.objects.size();++i)if(s.objects[i].classId.value==7){const auto pos=s.objects[i].position;need(pos.x==std::clamp(p.x,0.f,6335.f)&&pos.y==std::clamp(p.y,0.f,3167.f),"effect construction preserves clamped float coordinates within exact 3DI extent");}}
}
Bytes encode(const WorldSnapshot& w){SaveGame g;g.currentLevel="TEST/CAMERA.3DG";g.levels[g.currentLevel].world=w;Bytes b;std::string e;need(encodeSave(g,b,e),e.c_str());return b;}
void context(){
 const auto l=level();auto s=scene();GameWorld w;load(w,s,l);const auto snap=w.snapshot();const auto before=encode(snap);NativeEffectCamera outside;outside.offsetX=-2000;w.setEffectCamera(outside);need(encode(w.snapshot())==before,"camera context is excluded from save bytes");
 SaveGame decoded;std::string e;need(decodeSave(before,decoded,e),e.c_str());need(w.restoreSnapshot(decoded.levels.at(s.mapPath).world,e),e.c_str());w.damage(1,1,0);need(count(w,s,3)==0,"restore retains currently bound outside camera context");
 auto clone=scene();GameWorld inside;load(inside,clone,l);inside.setEffectCamera({});need(inside.restoreSnapshot(decoded.levels.at(clone.mapPath).world,e),e.c_str());inside.damage(1,1,0);need(count(inside,clone,3)==1,"same save uses independently bound inside camera context");
}
}
int main(){helper();death();NativeEffectCamera c;c.offsetX=-2000;projectile({500,500},c,false,true);projectile({500,500},{},true,true);
 c={};c.modeFlags=0x8000;c.viewWidth=c.viewHeight=1;c.offsetX=c.offsetY=-64;projectile({-3,-3},c,true);
 c.offsetX=-6271;c.offsetY=-3103;projectile({6403,3203},c,true);
 c={};projectile({500.9f,500.25f},c,true);c.offsetX=-2000;projectile({500.9f,500.25f},c,false);context();std::cout<<"effect camera boundaries, death/Hit-fx admission, delayed payload and save context passed\n";}
