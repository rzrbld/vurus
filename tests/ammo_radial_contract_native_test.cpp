#include "game/ammo_radial_contract_native.h"
#include "game/health_damage_native.h"
#include "fixtures/native_radial_contract_records.h"
#include "fixtures/native_direct_credit_records.h"
#include <array>
#include <bit>
#include <iostream>
#include <stdexcept>
using namespace vyruz;
static void need(bool value,const char* why){if(!value)throw std::runtime_error(why);}
int main(){try {
 unsigned count=0;
 for(const auto& r:native_radial_fixture::records) {
  const unsigned mode=r[0]-100;
  const auto number=[&](unsigned n){return std::bit_cast<float>(r[n]);};
  std::array<NativeCreatorNode,4> nodes{};nodes[0].generation=123;nodes[1].generation=456;
  nodes[2].creator=0;nodes[2].creatorGeneration=123;
  if(mode==6)nodes[2].creator.reset();
  if(mode==22 || mode==24){nodes[2].creator=1;nodes[2].creatorGeneration=456;nodes[1].creator=0;nodes[1].creatorGeneration=mode==24?122:123;}
  if(mode==23)nodes[2].creatorGeneration=122;
  const auto ancestry=nativeCreatorAncestry(nodes,mode==15?3:2,0);
  if(ancestry.staleLinkNode)nodes[*ancestry.staleLinkNode].creator.reset();
  NativeRadialAdmissionInput admission;admission.targetEnergy=number(7);admission.targetSubtype=0x16;
  admission.sourcePresent=true;admission.sourceSubtype=mode==8||mode==14?0x11:0x10;
  admission.sourcePlayerOwned=ancestry.owned;admission.targetAge=admission.sourceAge=1000;admission.tickMilliseconds=16;
  if(mode==1)admission.targetKind=2;
  if(mode==2)admission.targetSubtype=0x11;
  if(mode==4)admission.targetFlags=0x200000;
  if(mode==5)admission.targetFlags=0x800000;
  if(mode==6||mode==7)admission.targetClassFlags20=0x40;
  if(mode==8||mode==9)admission.targetFlags=0x400000;
  if(mode==10||mode==11){admission.targetSubtype=0x12;admission.onlyForPlayerItem=mode==10;}
  if(mode==12||mode==13){admission.targetSubtype=0x10;admission.targetAge=mode==12?1016:1017;}
  if(mode==14)admission.targetSubtype=0x10;
  if(mode==15)admission.targetIsSource=true;
  if(mode==16||mode==17)admission.targetFlags=8;
  if(mode==18||mode==19)admission.targetClassFlags20=2;
  if(mode==19)admission.targetFlags=0x20;
  if(mode==20)admission.targetFlags=0x40;
  if(mode==29)admission.targetClassFlags20=0x20;
  float energy=admission.targetEnergy,shield=number(5);auto flags=admission.targetFlags;
  unsigned ownerKills=0,fastKills=0,callbacks=0,wakes=0,visitation=0;float amount=0;
  const unsigned visits=mode==25?0:mode==26?2:1;
  for(unsigned visit=0;visit<visits;++visit) {
   admission.targetEnergy=energy;admission.targetFlags=flags;
   if(!nativeRadialAdmitted(admission))continue;
   amount=number(1);
   NativeHealthDamageInput health;health.energy=energy;health.shield=shield;health.shieldMinimum=number(6);
   health.actorFlags=flags;health.behaviorFlags=mode==16?3:mode==31?8:9;
   health.classFlags1c=mode==30?0:0x10;health.classFlags20=admission.targetClassFlags20;
   const auto result=nativeHealthDamage(health,amount);energy=result.energy;shield=result.shield;
   callbacks+=result.lethalClassCallback;wakes+=result.awakeRequested;if(result.awakeRequested){flags|=0x8000000;++visitation;}
   NativeAmmoKillCreditInput credit;credit.targetEnergy=energy;credit.targetFlags=flags;credit.targetClassFlags20=admission.targetClassFlags20;
   credit.targetSubtype=admission.targetSubtype;credit.sourcePresent=true;credit.sourceIsAmmo=admission.sourceSubtype==0x10;
   credit.sourcePlayerOwned=ancestry.owned;credit.immediateOwnerValid=nodes[2].creator.has_value();credit.immediateOwnerIsPlayer=nodes[2].creator==0;
   credit.targetCountsAsEnemy=mode!=11&&mode!=13&&mode!=14&&mode!=31;
   const auto kill=nativeAmmoKillCredit(credit,true);ownerKills+=kill.ownerKill;fastKills+=kill.fastKill;
   if(kill.markCounted)flags|=0x40;flags=nativeRadialHitFlags(flags);
  }
  const bool good=std::bit_cast<std::uint32_t>(energy)==r[8] && std::bit_cast<std::uint32_t>(shield)==r[9] && flags==r[10] && ownerKills==r[11] && fastKills==r[12] && (fastKills?500u:0u)==r[13] && nodes[2].creator.has_value()==bool(r[14]) && callbacks==r[15] && wakes==r[16] && visitation==r[17] && std::bit_cast<std::uint32_t>(amount)==r[18];
  if(!good)throw std::runtime_error("native radial contract fixture "+std::to_string(count)+" mode="+std::to_string(mode));
  ++count;
 }
 unsigned directCount=0;
 for(const auto& r:native_direct_credit_fixture::records){
  NativeAmmoKillCreditInput in;in.targetSubtype=r[0];in.targetEnergy=std::bit_cast<float>(r[3])-std::bit_cast<float>(r[4]);
  in.targetFlags=r[1]==1?0x40:r[1]==2||r[1]==3?0x20:0;in.targetClassFlags20=r[1]==2?2:0;
  in.sourcePresent=in.sourceIsAmmo=true;in.immediateOwnerValid=r[2]==1||r[2]==3;in.immediateOwnerIsPlayer=r[2]==1;
  const auto out=nativeAmmoKillCredit(in,false);
  if(std::bit_cast<std::uint32_t>(in.targetEnergy)!=r[5] || r[6]!=0 ||
     (in.targetFlags|(out.markCounted?0x40:0))!=r[7] || out.ownerKill!=bool(r[8]) ||
     in.immediateOwnerValid!=bool(r[9]) || r[10]!=0 || out.ownerCommentary!=bool(r[11]) || out.fastKill)
   throw std::runtime_error("native direct credit fixture "+std::to_string(directCount));
  ++directCount;
 }
 NativeAmmoKillCreditInput credit;credit.targetEnergy=-1;credit.sourcePresent=credit.sourceIsAmmo=credit.sourcePlayerOwned=credit.immediateOwnerValid=credit.immediateOwnerIsPlayer=credit.targetCountsAsEnemy=true;
 for(unsigned subtype=0x10;subtype<=0x20;++subtype){credit.targetSubtype=subtype;auto direct=nativeAmmoKillCredit(credit,false);need(!direct.fastKill,"direct never Fast Kill");need(direct.ownerKill==(subtype>=0x16),"direct native subtype credit mask");need(nativeAmmoKillCredit(credit,true).ownerKill,"radial has no direct subtype mask");}
 credit.immediateOwnerValid=false;need(!nativeAmmoKillCredit(credit,false).markCounted,"direct missing owner leaves counted flag");need(nativeAmmoKillCredit(credit,true).markCounted,"radial missing owner still marks counted");
 credit.targetFlags=0x20;credit.targetClassFlags20=0;need(nativeAmmoKillCredit(credit,true).markCounted,"Only-base requires Two-pieces class flag");credit.targetClassFlags20=2;need(!nativeAmmoKillCredit(credit,true).markCounted,"Two-pieces lower piece excludes credit");
 NativeRadialAdmissionInput age;age.sourcePresent=true;age.sourceSubtype=age.targetSubtype=0x10;age.targetEnergy=1;age.sourceAge=0xfffffff8;age.targetAge=8;age.tickMilliseconds=16;need(!nativeRadialAdmitted(age),"native age subtraction wraps");age.targetAge=9;need(nativeRadialAdmitted(age),"next native age beyond tick admitted");
 std::cout<<"native radial admission/ancestry/credit observed fixtures="<<count<<" direct credit fixtures="<<directCount<<" and edge contracts passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
