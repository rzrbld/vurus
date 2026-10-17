#include "game/native_awake_manager.h"
#include "game/health_damage_native.h"
#include <array>
#include <bit>
#include <iostream>
#include <stdexcept>
using namespace vyruz;
static void need(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
static void word(std::uint64_t& digest,std::uint32_t value){for(unsigned byte=0;byte<4;++byte){digest^=(value>>(8*byte))&255;digest*=1099511628211ull;}}
// Expected digests are from retained original x86 execution, not from this
//helper. Hash the complete fixed-layout output records in native little endian.
int main(){try{
 std::uint64_t digest=14695981039346656037ull;unsigned records=0,power=1;
 for(unsigned length=0;length<=6;++length){for(unsigned code=0;code<power;++code){auto x=code;std::array<unsigned,6> digits{};unsigned used=0;bool duplicate=false;
    for(unsigned i=0;i<length;++i){digits[i]=x%5;x/=5;if(digits[i]<4){duplicate|=bool(used&(1u<<digits[i]));used|=1u<<digits[i];}}
    if(duplicate)continue;
    for(unsigned mask=0;mask<16;++mask){NativeAwakeManager m;std::array<unsigned,4> ages{1000,1000,1000,1000},indices{77,77,77,77};
        for(unsigned i=0;i<length;++i){m.append(digits[i]<4?digits[i]:m.Missing);if(digits[i]<4)indices[digits[i]]=i;}
        std::size_t cursor=0;while(const auto id=m.next(cursor,[&](auto id){return !(mask&(1u<<id));},[&](auto id,auto index){indices[id]=index;}))ages[*id]+=16;
        word(digest,length);word(digest,code);word(digest,mask);word(digest,m.entries.size());
        for(unsigned i=0;i<6;++i)word(digest,i<m.entries.size()?m.entries[i]:UINT32_MAX);
        for(auto age:ages)word(digest,age);for(auto index:indices)word(digest,index);word(digest,1);++records;
    }
 }power*=5;}
 need(records==29680&&digest==0x35ac4379179cb385ull,"all29680 original manager hole/index/age records match");
 digest=14695981039346656037ull;records=0;
 const auto capture=[&](unsigned kind,unsigned count,unsigned initialFlags,unsigned initialIndex,unsigned repeats,unsigned cls,unsigned behavior){
    NativeAwakeManager m;for(unsigned i=0;i<count;++i)m.append(i);
    unsigned flags=initialFlags,index=initialIndex,warnings=0;float energy=100,shield=50;
    if(!kind)for(unsigned n=0;n<repeats;++n){if(nativeAwakeEnrollmentAllowed(flags&0x8000000,index)){index=m.append(4);flags|=0x8000000;}else ++warnings;}
    else {NativeHealthDamageInput input;input.energy=100;input.shield=50;input.actorFlags=flags;input.classFlags1c=cls;input.behaviorFlags=behavior;const auto out=nativeHealthDamage(input,25);if(out.awakeRequested){index=m.append(4);flags|=0x8000000;}energy=out.energy;shield=out.shield;}
    for(auto value:{kind,count,initialFlags,initialIndex,repeats,cls,behavior,unsigned(m.entries.size()),flags,index,warnings})word(digest,value);
    for(unsigned i=0;i<8;++i)word(digest,i<m.entries.size()?m.entries[i]:UINT32_MAX);
    word(digest,std::bit_cast<std::uint32_t>(energy));word(digest,std::bit_cast<std::uint32_t>(shield));++records;
 };
 for(unsigned count=0;count<5;++count)for(unsigned awake=0;awake<2;++awake)for(unsigned index=0;index<6;++index)for(unsigned repeats=1;repeats<=3;++repeats)capture(0,count,awake?0x8000000:0,index,repeats,0,0);
 for(unsigned managed=0;managed<2;++managed)for(unsigned awake=0;awake<2;++awake)for(unsigned index=0;index<3;++index)for(unsigned immunity=0;immunity<4;++immunity)for(unsigned hidden=0;hidden<2;++hidden)for(unsigned behavior=0;behavior<2;++behavior)
    capture(1,2,(awake?0x8000000:0)|(immunity&1?0x200000:0)|(immunity&2?0x800000:0)|(hidden?8:0),index,0,managed?0x10:0,behavior?3:1);
 need(records==372&&digest==0x57f6f042420e043cull,"all372 original enrollment/health/duplicate-warning records match");
 digest=14695981039346656037ull;records=0;
 // Whole4406e0 observed same-pass append, with only its product/release callee
 // replaced by an explicit event that invokes actual419ca0 enrollment.
 for(unsigned length=1;length<=6;++length)for(unsigned source=0;source<length;++source)for(unsigned mask=0;mask<(1u<<length);++mask){if(mask&(1u<<source))continue;
  for(unsigned births=0;births<=4;++births){NativeAwakeManager m;std::array<unsigned,10> ages,indices;ages.fill(1000);indices.fill(77);
   for(unsigned i=0;i<length;++i){m.append(mask&(1u<<i)?m.Missing:i);if(!(mask&(1u<<i)))indices[i]=i;}
   std::size_t cursor=0;unsigned releases=0;while(const auto id=m.next(cursor,[](auto){return true;},[&](auto id,auto index){indices[id]=index;})){ages[*id]+=16;if(*id==source){m.erase(indices[*id]);++releases;for(unsigned birth=0;birth<births;++birth)indices[6+birth]=m.append(6+birth);}}
   for(auto value:{length,source,mask,births,unsigned(m.entries.size())})word(digest,value);
   for(unsigned i=0;i<10;++i)word(digest,i<m.entries.size()?m.entries[i]:UINT32_MAX);
   for(auto age:ages)word(digest,age);for(auto index:indices)word(digest,index);word(digest,1);word(digest,releases);++records;
  }
 }
 need(records==1605&&digest==0xee22d57e34afe2c3ull,"all1605 original dynamic append/product-event records match");
 digest=14695981039346656037ull;records=0;
 const std::array<std::uint32_t,3> displacementBits{0,0x3f800000,0x7fc00000};
 for(unsigned kind=1;kind<=3;kind+=2)for(unsigned subtype=0xf;subtype<=0x21;++subtype)for(unsigned classKind=0;classKind<3;++classKind)for(unsigned awake=0;awake<2;++awake)for(unsigned match=0;match<2;++match)for(auto dx:displacementBits)for(auto dz:displacementBits){
  const auto decision=nativeAwakeRestDecision(std::bit_cast<float>(dx),std::bit_cast<float>(dz),kind,subtype,classKind!=0,classKind==2?0x10:0,awake?0x8000000:0,25,match?25:26);
  for(auto value:{kind,subtype,classKind,awake,match,dx,dz,unsigned(decision.rested),awake&&!decision.sleep?0x8000000u:0u,decision.sleep?0u:1u,unsigned(decision.sleep),decision.rested?0x42a00000u:0x11u,decision.rested?0x42c80000u:0x22u,decision.rested?0u:0x33u,decision.rested?0u:0x44u})word(digest,value);++records;
 }
 need(records==4104&&digest==0x6cae86568e2ef8b5ull,"all4104 original rest/sleep branch state records match");
 std::cout<<"native awake manager29680 dispatcher,372 enrollment/health,1605 dynamic-append and4104 rest/sleep records passed\n";
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
