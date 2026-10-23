#include "audio/game_audio.h"
#include "assets/gaf_audio.h"
#include "assets/gaf.h"
#include "game/world.h"
#include "render/scene.h"
#include <iostream>
#include <stdexcept>
#include <array>
using namespace vyruz;
namespace {
void need(bool ok,const std::string& why){if(!ok)throw std::runtime_error(why);}
using Bytes = std::vector<uint8_t>;
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
Bytes graphic(uint32_t format = 0x40000322, uint32_t storedBytes = 8,
              uint32_t secondBitmap = 0, bool multi=false,uint32_t command=0x10001,uint32_t sample=1) {
    Bytes descriptor(44); put(descriptor, 0, 1); put(descriptor, 4, 3);
    put(descriptor, 8, 1); put(descriptor, 16, static_cast<uint32_t>(-2));
    put(descriptor, 20, 5);
    // Deliberately differ from the usual -origin-size formula: preserve disk values.
    put(descriptor, 28, static_cast<uint32_t>(-7)); put(descriptor, 32, 9);
    Bytes bitmap(76);
    bitmap[0] = 'B'; bitmap[1] = 'M'; bitmap[2] = 'P'; bitmap[3] = '_';
    put(bitmap, 4, storedBytes); put(bitmap, 8, format); put(bitmap, 12, 56);
    put(bitmap, 16, format); put(bitmap, 20, 1); put(bitmap, 24, 16);
    put(bitmap, 28, 16); put(bitmap, 32, 3); put(bitmap, 36, 1);
    put(bitmap, 40, 8); put(bitmap, 60, 0);
    // RGB555 red, black, magenta, then padding (not a fourth pixel).
    bitmap[68] = 0; bitmap[69] = 0x7c;
    bitmap[72] = 0x1f; bitmap[73] = 0x7c;
    bitmap[74] = 0xaa; bitmap[75] = 0xbb;
    Bytes sequence(32); put(sequence, 0, 2); put(sequence, 4, 7); put(sequence, 8, 2);
    Bytes poses(56); put(poses, 0, 1); put(poses, 4, static_cast<uint32_t>(-3));
    put(poses, 8, 4); put(poses, 12, 0x108); put(poses, 24, 8);
    put(poses,16,command);put(poses,20,sample);put(poses,44,command);put(poses,48,sample);put(poses, 28, secondBitmap); put(poses, 52, 110);
    if(multi){auto descriptor2=descriptor;put(descriptor2,16,17);append(descriptor,descriptor2);
        auto bitmap2=bitmap;bitmap2[68]=0xe0;bitmap2[69]=0x03;append(bitmap,bitmap2);put(poses,0,2);put(poses,28,1);}
    Bytes headG(36), headA(36); put(headG, 0, multi?2:1); put(headA, 0, 1);
    Bytes head; append(head, chunk("GOB_", headG)); append(head, chunk("ANM_", headA));
    Bytes data; append(data, chunk("GOB_", streams({descriptor, bitmap, {}, {}})));
    append(data, chunk("ANM_", streams({sequence, {}, poses, {}})));
    Bytes root; append(root, chunk("HEAD", head, 2)); append(root, chunk("DATA", data, 2));
    return chunk("GAF_", root, 2);
}
Bytes audioBank(unsigned count){
 Bytes descriptors(count*76),pcm;
 for(unsigned i=0;i<count;++i){Bytes snd(36+24000);snd[0]='S';snd[1]='N';snd[2]='D';snd[3]='_';put(snd,4,24000);put(snd,12,24);put(snd,20,24000);put(snd,24,8);put(snd,28,1);put(snd,32,24000);std::fill(snd.begin()+36,snd.end(),uint8_t(144+i));append(pcm,snd);}
 Bytes hg(36);put(hg,0,count);Bytes hp=chunk("SMP_",hg),dp=chunk("SMP_",streams({descriptors,pcm,{},{}}));Bytes root=chunk("HEAD",hp,1);append(root,chunk("DATA",dp,1));return chunk("GAF_",root,2);
}
TextNode value(std::string k,std::string v){TextNode n;n.kind=TextKind::Assignment;n.key=k;n.values={v};return n;}
TextNode block(std::string k,std::vector<TextNode> c){TextNode n;n.kind=TextKind::Block;n.key=k;n.children=c;return n;}
}
int main(){try{
 // Exercise actual renderer actor and terrain producers without a GL context.
 const auto gfxBank=graphic();ContentReader graphicReader=[&](const auto&,auto& out){out=gfxBank;return true;};
 SceneContent visual;ObjectClass cls;cls.id={0};cls.name="Visual";cls.sprite.path="local.gaf";cls.sprite.sequence={1};visual.classes={cls};ObjectPlacement object;object.id={0};object.classId={0};object.sprite=cls.sprite;visual.objects={object};std::string error;
 SceneRenderer renderer;need(renderer.load(visual,graphicReader,error),error);renderer.advance(1);auto actorCues=renderer.takeAudioCues();need(!actorCues.empty()&&actorCues[0].sharedBank&&actorCues[0].sample==1,"actor producer retains shared command and sample");
 Gfx unused;renderer.clear(unused);
 NativeTileBodyCatalog catalog;catalog.width=catalog.height=1;catalog.sources.resize(2);catalog.sources[1]={1,2,1,"","","local.gaf",1};catalog.cells={{{1,0,0,0}}};
 need(renderer.load({},graphicReader,error)&&renderer.loadTerrain(catalog,error),error);EnemyAnimationView view;view.unboundedOracle=true;std::vector<ActorState> none;
 renderer.advance(10001);renderer.advanceEnemyPhase(2000,none,view,EnemyAnimationPhase::VisibleUpdate);auto terrainCues=renderer.takeAudioCues();need(!terrainCues.empty()&&terrainCues[0].sharedBank&&terrainCues[0].source.has_value(),"terrain producer retains shared command and fixed source");renderer.clear(unused);
 SceneContent scene;scene.mapPath="TEST/SHARED-SAMPLES.3DG";cls.name="Player";cls.behaviours={"Player"};cls.sprite={};cls.sprite.path="local.gaf";cls.properties={value("Managed","1"),value("Sub-type","Vyruz"),block("Energy",{value("Initial","100"),value("Max","100")})};scene.classes={cls};object.properties=cls.properties;object.behaviours=cls.behaviours;object.position={200,200};object.sprite={};scene.objects={object};scene.player=ObjectId{0};Level level;level.mapW=level.mapH=32;level.hasTerrainCollision=true;GameWorld world;need(world.load(scene,level,error),error);
 const auto local=audioBank(2),shared=audioBank(1),empty=audioBank(0);size_t reads=0;ContentReader reader=[&](const auto& name,auto& out){++reads;const auto path=folded(name);if(path=="common/data/panel/panel.gaf")out=gfxBank;else if(path=="local.gaf")out=local;else if(path=="shared.gaf")out=shared;else if(path=="empty.gaf")out=empty;else if(path=="bad.gaf")out={1,2,3};else return false;return true;};
 SDL_setenv("SDL_AUDIODRIVER","dummy",0);GameAudio audio;need(audio.device().open(error),error);audio.device().setPaused(true);
 auto exercise=[&](std::string binding,bool flag,unsigned sample,bool expected,bool terrain){
  need(audio.load(scene,reader,"ENGLISH",error,nullptr,binding),error);audio.consume(world,scene,{});audio.device().stopNativeChannels(0xffff);const auto loaded=reads;
  SceneAudioCue cue=actorCues[0];cue.bankPath="local.gaf";cue.sharedBank=flag;cue.sample=sample;if(terrain)cue.source=std::array<float,3>{200,200,24};audio.consume(world,scene,{cue});bool playing=false;for(unsigned i=1;i<=16;++i)playing|=audio.device().nativeChannelPlaying(i);need(playing==expected,"shared/local selection and selected-bank index validation");need(reads==loaded,"no playback-time resource reads");
  audio.reset();audio.consume(world,scene,{cue});playing=false;for(unsigned i=1;i<=16;++i)playing|=audio.device().nativeChannelPlaying(i);need(playing==expected,"transient reset retains sample binding");
  const auto after=reads;need(audio.load(scene,reader,"ENGLISH",error,nullptr,binding),error);need(reads==after,"same-load binding uses cached banks");
 };
 for(bool terrain:{false,true}){
  exercise("",true,2,true,terrain); // No shared bank: local fallback.
  exercise("shared.gaf",false,2,true,terrain); // Unflagged always local.
  exercise("shared.gaf",true,1,true,terrain);
  exercise("shared.gaf",true,2,false,terrain); // No index-based local fallback.
  exercise("shared.gaf",true,0,false,terrain);
  exercise("empty.gaf",true,1,false,terrain); // Present empty is not absent.
  exercise("missing.gaf",true,2,true,terrain);
  exercise("bad.gaf",true,2,true,terrain);
 }
 audio.device().close();std::cout<<"PASS actor/terrain shared-sample producers, native fallback/index rules, reset, cache and SDL mixer admissions\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
