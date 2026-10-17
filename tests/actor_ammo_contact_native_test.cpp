#include "game/actor_ammo_contact_native.h"
#include <array>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace vyruz;
struct Record {uint32_t sourceKind,subtype,targetKind,targetSubtype,attr,owner,protectedFlag,action,flags,loop,callOK,pointOK;};
static void need(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
int main(int argc,char**argv){try{
 need(argc==1||argc==2,"usage: actor_ammo_contact_native_test [NATIVE_RECORDS]");std::ifstream in;if(argc==2){in.open(argv[1],std::ios::binary);need(bool(in),"missing native records");}
 uint64_t fingerprint=14695981039346656037ull;unsigned count=0,failed=0;std::array<unsigned,4> actions{};
 for(unsigned sk=0;sk<2;++sk)for(unsigned st=0;st<64;++st)for(unsigned tk=0;tk<2;++tk)for(unsigned ts=0;ts<2;++ts)for(unsigned a=0;a<6;++a)for(unsigned owner=0;owner<4;++owner)for(unsigned pf=0;pf<2;++pf){
  NativeAmmoContactObject source,selected;source.identity=1;source.generation=7;source.kind=sk?3:2;source.subtype=st;
  selected.identity=2;selected.kind=tk?3:1;selected.subtype=ts?22:16;selected.creator=owner==1||owner==2?1:owner==3?3:0;selected.creatorGeneration=owner==2?8:7;
  std::array<NativeActorAmmoAttribute,2> attributes{{{a==1?9u:0xbu,a==3||a==4?4u:0u},{0xb,a==5?4u:0u}}};const size_t attributeCount=a? a>=4?2:1:0;
  const auto result=nativeActorAmmoDispatch(source,selected,pf?0x800000:0,std::span(attributes).first(attributeCount));
  Record actual{source.kind,st,selected.kind,selected.subtype,a,owner,pf,static_cast<uint32_t>(result.action),result.sourceFlags,result.action==NativeActorAmmoAction::ImpactAndAbortActor?18u:17u,1,1};
  if(result.action==NativeActorAmmoAction::ImpactAndAbortActor)need(result.attribute==0,"first Ammo attribute identity");
  if(argc==2){Record native{};need(bool(in.read(reinterpret_cast<char*>(&native),sizeof native)),"short native record file");
   if(std::memcmp(&native,&actual,sizeof native)!=0){if(failed<4)std::cerr<<"mismatch source="<<source.kind<<'/'<<st<<" target="<<selected.kind<<'/'<<selected.subtype<<" attr="<<a<<" owner="<<owner<<" protected="<<pf<<'\n';++failed;}}
  const auto* bytes=reinterpret_cast<const unsigned char*>(&actual);for(size_t n=0;n<sizeof actual;++n){fingerprint^=bytes[n];fingerprint*=1099511628211ull;}
  ++actions[actual.action];++count;
 }
 if(argc==2){char extra;need(!in.read(&extra,1),"extra native records");}
 need(count==24576,"native case count");need(fingerprint==0x9b74d6eb921aa125ull,"native instruction record fingerprint mismatch");need(failed==0,"native instruction comparison mismatch");
 std::cout<<count<<" original actor-to-Ammo branch records, "<<failed<<" mismatches; physical="<<actions[0]<<" nextInteraction="<<actions[1]<<" clear="<<actions[2]<<" impactAbort="<<actions[3]<<'\n';return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
