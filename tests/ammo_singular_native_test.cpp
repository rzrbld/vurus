#include "game/ammo_collision_native.h"
#include "game/native_impact_coordinate.h"
#include <bit>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace vyruz;
static void need(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
struct Record{uint32_t x,z,a,b,keep;};
int main(int argc,char**argv){try{need(argc==1||argc==2,"usage: ammo_singular_native_test [native-selector-records]");std::ifstream in;if(argc==2){in.open(argv[1],std::ios::binary);need(bool(in),"missing original selector records");}
 constexpr uint32_t values[]={0,0x3f800000u,0xbf800000u,0x41200000u,0x7fc00000u,0xffc00000u};
 uint64_t hash=14695981039346656037ull;unsigned count=0;
 for(auto x:values)for(auto z:values)for(auto a:values)for(auto b:values){Record r{x,z,a,b,uint32_t(nativeAmmoPreviousContactCloser({std::bit_cast<float>(x),std::bit_cast<float>(z)},{std::bit_cast<float>(a),std::bit_cast<float>(b)}))};
  if(argc==2){Record original{};need(bool(in.read(reinterpret_cast<char*>(&original),sizeof original)),"short original selector records");need(original.x==x&&original.z==z&&original.a==a&&original.b==b&&original.keep==r.keep,"original finite/unordered selector result");}
  const auto* bytes=reinterpret_cast<const unsigned char*>(&r);for(unsigned n=0;n<sizeof r;++n){hash^=bytes[n];hash*=1099511628211ull;}++count;
 }
 need(hash==0x4289bd154eef32b8ull,"unchanged-original selector fingerprint");
 if(argc==2){char extra;need(!in.read(&extra,1),"extra selector records");}
 const float nan=std::bit_cast<float>(0xffc00000u);
 need(nativeImpactCoordinateWord(nan)==0,"original indefinite __ftol low word");
 need(nativeImpactCoordinateWord(17.75f)==17&&nativeImpactCoordinateWord(-17.75f)==0xffffffefu,"original truncating low word");
 need(nativeImpactCoordinateEqual(nan,5)&&nativeImpactCoordinateEqual(5,nan)&&!nativeImpactCoordinateEqual(5,6),"original C3 unordered equality");
 need(nativeImpactFxClamp(nan,1024)==0&&nativeImpactFxClamp(20,1024)==20,"original independent FX clamp");
 NativeAmmoContactSelector selector;selector.proposed={20,0};selector.consider(1,0x10,{nan,nan});selector.consider(2,0x16,{20,0});need(selector.selected->reference==1,"original indefinite first-contact retention");
 std::cout<<"PASS "<<count<<" original finite/unordered selector records and singular coordinate/priority controls\n";return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
