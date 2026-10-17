#include "game/health_damage_native.h"
#include "fixtures/native_health_damage_records.h"
#include <bit>
#include <cstdint>
#include <iostream>
#include <stdexcept>
using namespace vyruz;
int main(){try {
 unsigned count=0;
 for(const auto& r:native_health_fixture::records) {
  const auto number=[&](unsigned n){return std::bit_cast<float>(r[n]);};
  NativeHealthDamageInput in;in.actorFlags=r[1];in.behaviorFlags=r[2];in.classFlags1c=r[3];in.classFlags20=r[4];
  in.shield=number(5);in.energy=number(6);in.shieldMinimum=number(8);
  in.worldPresent=r[0]!=9;in.managerPresent=r[0]!=10;in.targetPresent=r[0]!=11;in.classPresent=r[0]!=12;
  const auto out=nativeHealthDamage(in,number(7));
  if(std::bit_cast<std::uint32_t>(out.shield)!=r[9] || std::bit_cast<std::uint32_t>(out.energy)!=r[10] ||
     out.lethalClassCallback!=bool(r[11]) || out.awakeRequested!=bool(r[12]) ||
     r[13]!=(r[1]|(out.awakeRequested?0x08000000:0)))
   throw std::runtime_error("native43ca30 fixture "+std::to_string(count));
  ++count;
 }
 // Result flags distinguish a shield-only hit from the energy path; neither
 // asks the caller to destroy the actor or credit a new death transition.
 NativeHealthDamageInput in;in.classFlags1c=0x10;in.classFlags20=0x20;in.energy=-10;in.shield=100;
 auto out=nativeHealthDamage(in,1);
 if(!out.healthApplied || out.energyUpdated || out.lethalClassCallback || out.energy!=-10 || out.shield!=99)
  throw std::runtime_error("shield-only hit on already negative Energy");
 in.shield=0;out=nativeHealthDamage(in,1);
 if(!out.healthApplied || !out.energyUpdated || !out.lethalClassCallback || out.energy!=-11)
  throw std::runtime_error("direct still applies after radial lethality");
 in.actorFlags=0x200000;in.classFlags1c=0;out=nativeHealthDamage(in,1);
 if(!out.awakeRequested || out.healthApplied || out.energyUpdated || out.lethalClassCallback)
  throw std::runtime_error("awake precedes immunity rejection");
 std::cout<<"native health exact observed fixtures="<<count<<" and result contracts passed\n";
 return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
