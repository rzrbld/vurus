#include "snapshot_fixture.h"
#include "game/world.h"
#include "game/save_state.h"
#include "game/save_file.h"
#include <array>
#include <cstdlib>
#include <iostream>
using namespace vyruz;
namespace {
using Bytes = std::vector<uint8_t>;
void check(bool ok, const char* message) {
    if (!ok) { std::cerr << message << '\n'; std::exit(1); }
}
void put(Bytes& b, size_t off, uint32_t value) {
    for (int i = 0; i < 4; ++i) b[off + i] = static_cast<uint8_t>(value >> (8 * i));
}
void append(Bytes& b, const Bytes& tail) { b.insert(b.end(), tail.begin(), tail.end()); }
Bytes chunk(const char* tag, Bytes payload, uint32_t children = 0) {
    Bytes result(16);
    for (int i = 0; i < 4; ++i) result[i] = tag[i];
    put(result, 4, payload.size()); put(result, 12, children);
    append(result, payload); return result;
}
Bytes streams(const std::array<Bytes, 4>& components) {
    Bytes result(96);
    for (size_t i = 0; i < components.size(); ++i) {
        put(result, i * 24 + 16, components[i].size());
        append(result, components[i]);
        while (result.size() % 4) result.push_back(0);
    }
    return result;
}
Bytes fixture(uint32_t duration=2,uint32_t count=8,uint32_t flags=0) {
    Bytes descriptor(44); put(descriptor, 0, 1); put(descriptor, 4, 3);
    put(descriptor, 8, 1); put(descriptor, 16, static_cast<uint32_t>(-2));
    put(descriptor, 20, 5);
    // Deliberately differ from the usual -origin-size formula: preserve disk values.
    put(descriptor, 28, static_cast<uint32_t>(-7)); put(descriptor, 32, 9);
    Bytes bitmap(76);
    bitmap[0] = 'B'; bitmap[1] = 'M'; bitmap[2] = 'P'; bitmap[3] = '_';
    put(bitmap, 4, 8); put(bitmap, 8, 0x40000322); put(bitmap, 12, 56);
    put(bitmap, 16, 0x40000322); put(bitmap, 20, 1); put(bitmap, 24, 16);
    put(bitmap, 28, 16); put(bitmap, 32, 3); put(bitmap, 36, 1);
    put(bitmap, 40, 8); put(bitmap, 60, 0);
    // RGB555 red, black, magenta, then padding (not a fourth pixel).
    bitmap[68] = 0; bitmap[69] = 0x7c;
    bitmap[72] = 0x1f; bitmap[73] = 0x7c;
    bitmap[74] = 0xaa; bitmap[75] = 0xbb;
    Bytes sequence(32); put(sequence, 0, count); put(sequence, 4, flags); put(sequence, 8, 1);
    Bytes poses(count*28);for(uint32_t i=0;i<count;++i){put(poses,i*28,1);put(poses,i*28+24,duration);}
    Bytes headG(36), headA(36); put(headG, 0, 1); put(headA, 0, 1);
    Bytes head; append(head, chunk("GOB_", headG)); append(head, chunk("ANM_", headA));
    Bytes data; append(data, chunk("GOB_", streams({descriptor, bitmap, {}, {}})));
    append(data, chunk("ANM_", streams({sequence, {}, poses, {}})));
    Bytes root; append(root, chunk("HEAD", head, 2)); append(root, chunk("DATA", data, 2));
    return chunk("GAF_", root, 2);
}
TextNode value(std::string key,std::string text){TextNode n;n.kind=TextKind::Assignment;n.key=key;n.values={text};return n;}
TextNode block(std::string key,std::vector<TextNode> children,std::string arg={}){TextNode n;n.kind=TextKind::Block;n.key=key;n.children=children;if(!arg.empty())n.arguments={arg};return n;}
SceneContent scene() {
 SceneContent s;s.mapPath="CAVE/T1C1.3DG";
 ObjectClass player;player.id={0};player.name="Player";player.behaviours={"Player"};player.properties={block("Energy",{value("Initial","10000")})};
 ObjectClass bomb;bomb.id={1};bomb.name="Bomb";bomb.properties={block("Energy",{value("Initial","1")}),block("TYPE",{value("Damage","1000"),value("Damage-zone","100"),value("Boom-fx","Boom")},"Explosion")};
 ObjectClass target;target.id={2};target.name="Target";target.properties={block("Energy",{value("Initial","10000")}),value("Bounding-box-size","10")};
 ObjectClass fx;fx.id={3};fx.name="Boom";fx.sprite={"BOOM.GAF",{1}};fx.properties={block("TYPE",{},"FX")};
 s.classes={player,bomb,target,fx};
 auto place=[&](unsigned cls,float x,float y){ObjectPlacement o;o.id={unsigned(s.objects.size())};o.classId={cls};o.position={x,y};o.properties=s.classes[cls].properties;o.behaviours=s.classes[cls].behaviours;s.objects.push_back(o);};
 place(0,100,100);place(1,500,500);place(2,500,500);place(2,500+89.44272f,500+44.72136f);s.player=ObjectId{0};return s;
}
}
int main(){
 Level level;level.mapW=100;level.mapH=100;std::string error;
 for(unsigned duration:{2u,100u}){
  auto content=scene();GameWorld world;auto reader=[=](const std::string&,Bytes& b){b=fixture(duration);return true;};world.setEffectReader(reader);
  check(vyruz_test::loadManagedFixture(world,content,level,error),"load Explosion fixture");world.damage(1,1,0);
  check(world.actors().size()==5 && world.actors()[4].explosion.has_value(),"destruction transfers payload to authored FX");
  check(world.actors()[2].vitals.energy==10000,"no immediate damage on destruction");
  const int ticks=duration==2?6:31; // cursor4 reached after84ms;500ms wins for slow sequence.
  for(int i=0;i<ticks-1;++i)world.tick(1.f/60,{});
  check(world.actors()[2].vitals.energy==10000,"damage waits for native half-animation/timer");
  SaveGame save;save.currentLevel=content.mapPath;save.levels[save.currentLevel].world=world.snapshot();Bytes encoded;
  check(encodeSave(save,encoded,error),"encode active Explosion v6");SaveGame decoded;check(decodeSave(encoded,decoded,error),"decode active Explosion v6");
  auto restoredContent=scene();GameWorld restored;restored.setEffectReader(reader);check(vyruz_test::loadManagedFixture(restored,restoredContent,level,error),"load resumed fixture");
  check(restored.restoreSnapshot(decoded.levels[save.currentLevel].world,error),error.c_str());
  world.tick(1.f/60,{});restored.tick(1.f/60,{});
  check(world.actors()[2].vitals.energy==9000,"native delayed payload damages target");
  check(restored.actors()[2].vitals.energy==9000,"save-resume preserves exact damage tick");
  check(world.actors()[3].vitals.energy>=9990,"native radial boundary excludes radius edge");
  for(int i=0;i<120;++i)world.tick(1.f/60,{});
  check(world.actors()[2].vitals.energy==9000,"Explosion damage applied once");
 }
 // Native 45e7e0 + 444e41 controls using EXPLO sequence1/4 timing metadata.
 // Expected ticks are original-instruction results, not derived from the port
 // cursor. Exercise the real awake World path and a disk checkpoint immediately
 // before damage, then compare complete encoded continuation through expiry.
 const auto encode=[&](const GameWorld& world,const SceneContent& content){
  SaveGame save;save.currentLevel=content.mapPath;save.levels[save.currentLevel].world=world.snapshot();Bytes bytes;
  check(encodeSave(save,bytes,error),error.c_str());return bytes;
 };
 struct Timing {unsigned duration,count;std::array<int,4> ticks;};
 for(const auto& timing:{Timing{4,17,{28,25,24,12}},Timing{2,16,{14,13,12,6}}}) {
  const std::array<int,4> steps{14,16,17,33};
  for(size_t k=0;k<steps.size();++k) {
   const int dt=steps[k],damageTick=timing.ticks[k];
   const auto bytes=fixture(timing.duration,timing.count,5);
   auto reader=[&](const std::string&,Bytes& b){b=bytes;return true;};
   auto content=scene();GameWorld world;world.setEffectReader(reader);
   check(vyruz_test::loadManagedFixture(world,content,level,error),error.c_str());world.damage(1,1,0);
   for(int tick=1;tick<damageTick;++tick) {
    world.tick(float(dt)/1000,{});
    check(world.actors()[2].vitals.energy==10000,"original FX damage must not occur early");
   }
   SaveGame decoded;check(decodeSave(encode(world,content),decoded,error),error.c_str());
   auto resumedContent=scene();GameWorld resumed;resumed.setEffectReader(reader);
   check(vyruz_test::loadManagedFixture(resumed,resumedContent,level,error),error.c_str());
   check(resumed.restoreSnapshot(decoded.levels.at(content.mapPath).world,error),error.c_str());
   const int expiryTick=(int(timing.duration*14*timing.count)+dt-1)/dt;
   for(int tick=damageTick;tick<=expiryTick+2;++tick) {
    world.tick(float(dt)/1000,{});resumed.tick(float(dt)/1000,{});
    check(world.actors()[2].vitals.energy==9000,"FX damage matches original visit and fires once");
    check(encode(world,content)==encode(resumed,resumedContent),"FX exact disk continuation through damage and expiry");
    if(tick<expiryTick)check(world.actors()[4].alive,"FX remains live until sequence finishes");
    else check(!world.actors()[4].alive&&!world.actors()[4].explosion,"finished FX releases its payload");
   }
  }
 }
 // Native +5c is a retained sequence binding, not a post-advance frame test.
 // A due timer still fires when this visit also finishes the port cursor.
 {
  auto content=scene();GameWorld world;
  world.setEffectReader([](const std::string&,Bytes& b){b=fixture(1,4,5);return true;});
  check(vyruz_test::loadManagedFixture(world,content,level,error),error.c_str());world.damage(1,1,0);
  auto state=world.snapshot();state.actors[4].explosion->remainingMs=1;
  check(world.restoreSnapshot(state,error),error.c_str());world.tick(.1f,{});
  check(world.actors()[2].vitals.energy==9000,"due timer remains eligible on the finishing visit");
  check(!world.actors()[4].alive&&!world.actors()[4].explosion,"coarse-step finished FX retires");
 }
}
