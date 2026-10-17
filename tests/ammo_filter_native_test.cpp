#include "game/ammo_filter_native.h"
#include <stdexcept>
#include <fstream>
#include <iostream>
using namespace vyruz;
static void need(bool ok){if(!ok)throw std::runtime_error("native Ammo filter evidence mismatch");}
int main(int argc,char**argv){try{
 // These192 admission bits are outputs of complete original41a470 calls in
 // work/quirk-audit/n2-grenade-contacts/native-probe/, same ordered input grid.
 constexpr uint32_t original[]={0x33330033u,0x33333300u,0x33333333u,0x33330033u,0x3333300u,0x33033333u};
 std::ifstream records;if(argc==2)records.open(argv[1],std::ios::binary);
 unsigned n=0,failed=0;
 for(unsigned direction=0;direction<2;++direction)for(unsigned age=0;age<4;++age)for(unsigned identity=0;identity<3;++identity)for(unsigned ignore=0;ignore<2;++ignore)for(unsigned kind=0;kind<4;++kind){
  NativeAmmoContactObject source,target;source.identity=1;target.identity=2;source.generation=target.generation=42;
  source.subtype=target.subtype=22;auto& ammo=direction?target:source;auto& other=direction?source:target;
  ammo.subtype=16;other.subtype=kind==0?22:kind==1?18:kind==2?16:22;
  ammo.ageMs=age==0?0:age==1?499:age==2?500:1000;ammo.creator=identity?other.identity:0;ammo.creatorGeneration=identity==2?43:42;ammo.ignoreOwner=ignore;
  if(kind==3)source.shape=target.shape=2;
  bool expected=(original[n/32]>>(n%32))&1;
  if(argc==2){uint32_t row[8];need(bool(records.read(reinterpret_cast<char*>(row),sizeof row)));need(row[0]==direction&&row[1]==ammo.ageMs&&row[2]==identity&&row[3]==ignore&&row[4]==kind);need(bool(row[5])==expected);}
  if(nativeAmmoContactAdmitted(source,target)!=expected)++failed;++n;
 }
 NativeAmmoContactObject source,target;need(!nativeAmmoContactAdmitted(source,target));source.identity=target.identity=1;need(!nativeAmmoContactAdmitted(source,target));
 std::cout<<n<<" original native contact-filter cases, "<<failed<<" mismatches\n";return failed||n!=192;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}
}
