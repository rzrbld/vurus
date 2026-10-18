#include "game/save_file.h"
#include <cstdlib>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <algorithm>
#include <bit>
#include "host_temp.h"
using namespace vyruz;
void check(bool b,const char* reason){if(!b){std::cerr<<reason<<'\n';std::exit(1);}}
void reseal(std::vector<uint8_t>& bytes){uint64_t h=14695981039346656037ULL;for(size_t i=28;i<bytes.size();++i){h^=bytes[i];h*=1099511628211ULL;}for(size_t i=0;i<8;++i)bytes[20+i]=uint8_t(h>>(i*8));}
std::vector<uint8_t> turnMarker(const EnemyTurnState& s){
 std::vector<uint8_t> marker{1};
 for(int value:{s.bodyPose,s.bodyDesiredPose,s.bodyPreviousPose,s.gunPose,s.gunDesiredPose,s.gunPreviousPose,
               s.bodyPeriodMs,s.gunPeriodMs,s.bodyRemainingMs,s.gunRemainingMs,s.logicalHeading,s.orthogonalHeading,s.turnCommand,s.lean})
  for(unsigned byte=0;byte<8;++byte)marker.push_back(uint8_t(uint64_t(int64_t(value))>>(8*byte)));
 marker.push_back(s.fastTurn?1:0);
 for(int value:{s.gunLogicalHeading,s.gunCommand,s.gunPreviousCommand})
  for(unsigned byte=0;byte<8;++byte)marker.push_back(uint8_t(uint64_t(int64_t(value))>>(8*byte)));
 marker.push_back(s.gunInRange?1:0);marker.push_back(s.gunStateKnown?1:0);return marker;
}
std::vector<uint8_t> squareMarker(const std::optional<NativeGeneratorSquareState>& state){
 std::vector<uint8_t> marker{uint8_t(state.has_value())};if(state)for(float value:{state->centerX,state->centerZ,state->minX,state->minZ,state->maxX,state->maxZ})
  for(unsigned byte=0;byte<8;++byte)marker.push_back(uint8_t(uint64_t(std::bit_cast<uint32_t>(value))>>(8*byte)));return marker;
}
// Derive genuine v34 actor layout without guessing offsets through variable
// nativeMotion/Spider/actions/held state. A tagged copy changes ONLY the v35
// optional to unique known49byte values, locating absent and present fields.
// Match the complete original packet first; remove mapped original fields in
// descending byte order. This preserves all preexisting legacy assertions.
void strip35ActorFields(std::vector<uint8_t>& bytes,const SaveGame& fixture){
 if(bytes[8]==34)return;
 check(bytes[8]==35,"v35 generator-square converter input");std::string error;std::vector<uint8_t> expected;
 check(encodeSave(fixture,expected,error)&&bytes==expected,"v35 legacy converter begins with actual unmodified writer payload");
 auto tagged=fixture;for(auto&[path,level]:tagged.levels)normalizeSavedSurprise(level.world);if(tagged.restartLevel)normalizeSavedSurprise(tagged.restartLevel->world);
 struct Record {std::vector<uint8_t> tagged,original;size_t position=0;};std::vector<Record> records;unsigned ordinal=0;
 const auto tag=[&](ActorState& actor){const auto original=squareMarker(actor.nativeGeneratorSquare);const float base=21001.25f+128.f*ordinal++;
  actor.nativeGeneratorSquare=NativeGeneratorSquareState{base,base+16,base-1,base+15,base+1,base+17};records.push_back({squareMarker(actor.nativeGeneratorSquare),original,0});};
 const auto world=[&](WorldSnapshot& state){for(auto& actor:state.actors)tag(actor);for(auto& held:state.heldItems)tag(held.actor);};
 for(auto&[path,level]:tagged.levels)world(level.world);if(tagged.restartLevel)world(tagged.restartLevel->world);
 std::vector<uint8_t> taggedBytes;check(encodeSave(tagged,taggedBytes,error),error.c_str());
 for(auto& record:records){const auto at=std::search(taggedBytes.begin()+28,taggedBytes.end(),record.tagged.begin(),record.tagged.end());
  check(at!=taggedBytes.end()&&std::search(at+1,taggedBytes.end(),record.tagged.begin(),record.tagged.end())==taggedBytes.end(),"Unique actual v35 generator-square optional marker");record.position=size_t(at-taggedBytes.begin());}
 std::sort(records.begin(),records.end(),[](const auto&a,const auto&b){return a.position<b.position;});size_t delta=0;
 for(auto& record:records){record.position-=delta;check(record.position+record.original.size()<=bytes.size()&&std::equal(record.original.begin(),record.original.end(),bytes.begin()+record.position),"Actual absent/present v35 field at mapped actor boundary");delta+=record.tagged.size()-record.original.size();}
 for(auto record=records.rbegin();record!=records.rend();++record)bytes.erase(bytes.begin()+record->position,bytes.begin()+record->position+record->original.size());
 bytes[8]=34;for(unsigned byte=0;byte<8;++byte)bytes[12+byte]=uint8_t((bytes.size()-28)>>(8*byte));reseal(bytes);
}
// Remove the actual v34 World extension from a one-world absent-optionals
// fixture, preserving the earlier v31/v32/v33 tail and following renderer.
void strip34AbsentWorldTail(std::vector<uint8_t>& bytes,const SaveGame& fixture){
 strip35ActorFields(bytes,fixture);
 check(bytes[8]==34&&fixture.levels.size()==1&&!fixture.restartLevel,"v34 legacy converter fixture shape");
 const auto& world=fixture.levels.begin()->second.world;
 check(!world.nativeAmmoLifecycleKnown&&world.bareAmmoCells.heads.empty()&&world.bareAmmoCells.links.empty(),
       "Legacy fixture has fixed empty v32 compact-cell tail");
 check(!world.nativeAwake&&!world.nativePlayerFacing&&!world.nativePool&&
       world.nativePlayerPreviousSurface.x==0&&world.nativePlayerPreviousSurface.y==0,
       "Legacy fixture has absent v34 scheduling/facing/pool state");
 check(std::none_of(world.actors.begin(),world.actors.end(),[](const auto& actor){return actor.nativeAmmoContinuation;}),
       "Legacy fixture has empty v33 continuation vector");
 std::vector<uint8_t> marker;
 for(uint64_t value:{uint64_t(world.nativeWorldRandom.seed),uint64_t(world.nativeWorldRandom.counter),
                     uint64_t(world.nativeGlobalRandom.seed),uint64_t(world.nativeGlobalRandom.counter)})
  for(unsigned byte=0;byte<8;++byte)marker.push_back(uint8_t(value>>(8*byte)));
 const auto at=std::search(bytes.begin()+28,bytes.end(),marker.begin(),marker.end());
 check(at!=bytes.end()&&std::search(at+1,bytes.end(),marker.begin(),marker.end())==bytes.end(),"Locate unique native RNG World tail for v34 removal");
 // v32 known/cells/clocks57 + v33 empty-vector8, after four RNG words32.
 const auto start=at+32+57+8;
 check(size_t(bytes.end()-start)>=35&&start[0]==0&&start[1]==0&&start[34]==0,
       "Locate absent v34 awake/facing/pool optionals");
 for(unsigned i=2;i<18;++i)check(start[i]==0,"Locate zero v34 previous player surface");
 for(unsigned word=0;word<2;++word){const uint64_t value=word?world.nativeReplayRandom.counter:world.nativeReplayRandom.seed;
  for(unsigned byte=0;byte<8;++byte)check(start[18+8*word+byte]==uint8_t(value>>(8*byte)),"Locate v34 replay RNG context words");}
 bytes.erase(start,start+35);
}
void strip34AbsentActorFields(std::vector<uint8_t>& bytes,size_t start){
 check(start+10<=bytes.size()&&bytes[start]==0&&bytes[start+9]==0,"Locate absent v34 Hit/fire actor fields");
 for(size_t i=1;i<9;++i)check(bytes[start+i]==255,"Locate absent v34 actor pool identity");
 bytes.erase(bytes.begin()+start,bytes.begin()+start+10);
}
// Fixture-specific legacy conversion: every caller below has exactly one
// world, no retry, absent cell lists and absent ordinary AI. Remove actual v31
// and v34 bytes before the pre-existing older-version conversions; never just relabel
// a current-layout payload as a legacy version.
void strip31WorldFields(std::vector<uint8_t>& bytes,const SaveGame& fixture){
 strip34AbsentWorldTail(bytes,fixture);
 const auto& level=*fixture.levels.begin();const auto& world=level.second.world;
 check(!world.actorCells,"Legacy fixture has absent v31 actor cells");
 const size_t start=28+8+fixture.currentLevel.size()+8+8+level.first.size();
 check(start<bytes.size()&&bytes[start]==0,"Locate absent v31 cell producer at World start");bytes.erase(bytes.begin()+start);
 std::vector<uint8_t> marker;
 for(uint64_t value:{uint64_t(world.nativeWorldRandom.seed),uint64_t(world.nativeWorldRandom.counter),
                     uint64_t(world.nativeGlobalRandom.seed),uint64_t(world.nativeGlobalRandom.counter)})
  for(unsigned byte=0;byte<8;++byte)marker.push_back(uint8_t(value>>(8*byte)));
 const auto at=std::search(bytes.begin()+28,bytes.end(),marker.begin(),marker.end());
 check(at!=bytes.end()&&std::search(at+1,bytes.end(),marker.begin(),marker.end())==bytes.end(),"Locate unique v31 global RNG after v29 world RNG");
 check(!world.nativeAmmoLifecycleKnown && world.bareAmmoCells.heads.empty() && world.bareAmmoCells.links.empty(),"Legacy converter omits unknown v32 lifecycle");
 // v33 appends an empty continuation vector in these legacy fixtures.
 check(std::none_of(world.actors.begin(),world.actors.end(),[](const auto& actor){return actor.nativeAmmoContinuation;}),"Legacy fixture has no pending movement");
 check(size_t(bytes.end()-(at+32))>=65,"Locate v33 continuation tail");
 for(unsigned i=0;i<8;++i)check(*(at+32+57+i)==0,"Legacy fixture continuation vector is empty");
 bytes.erase(at+32+57,at+32+65);
 // v32 appends known flag, empty sparse-cell header/maps and three clocks.
 bytes.erase(at+32,at+32+57);
 bytes.erase(at+16,at+32);
 bytes[8]=31;
}
void strip31AbsentActorTail(std::vector<uint8_t>& bytes,size_t start){
 check(start+3<=bytes.size()&&bytes[start]==0&&bytes[start+1]==0&&bytes[start+2]==0,
       "Locate absent v31 pending/count/ordinary-AI actor extension");
 // v34 inserts Hit/pool/fire between v31 and v32; remove those real bytes.
 strip34AbsentActorFields(bytes,start+3);
 // v32 appends two generations and four owner words.
 check(start+51<=bytes.size(),"Locate v32 owner/generation actor extension");
 bytes.erase(bytes.begin()+start+3,bytes.begin()+start+51);
 bytes.erase(bytes.begin()+start,bytes.begin()+start+3);
}
int main(){
 {
  TextNode child;child.key="Usage-Count";child.values={"2"};
  TextNode node;node.key="TYPE";node.children={child};
  SharedProperties live{node};auto restart=live;auto worker=live;
  const auto* shared=live.read().data();
  check(restart.read().data()==shared&&worker.read().data()==shared,"Property copies share immutable definition storage");
  worker[0].children[0].values={"1"};
  check(live.read()[0].children[0].values[0]=="2"&&restart.read().data()==shared,"Nested worker mutation cannot change live/Restart properties");
  live.push_back(child);
  check(restart.size()==1&&live.size()==2,"Live mutation detaches from Restart");
  restart.clear();check(worker.size()==1&&live.size()==2,"Clearing one owner preserves other values");
 }

 // Captured with the unmodified version 1 writer: retain its original checksum and layout.
 const std::string legacyHex="5659525a5341564501000000d9000000000000000ada55c616332a600b000000000000004c4556454c2f412e33444701000000000000000b000000000000004c4556454c2f412e3344471503000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000001100000000000000a69b443b000000000000f042000000000000f0c10000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000";
 std::vector<uint8_t> legacy;for(size_t i=0;i<legacyHex.size();i+=2)legacy.push_back(uint8_t(std::stoul(legacyHex.substr(i,2),nullptr,16)));
 SaveGame migrated;std::string migrationError;if(!decodeSave(legacy,migrated,migrationError)){std::cerr<<"Migration failed: "<<migrationError<<" bytes="<<legacy.size()<<"\n";return 1;}
 const auto& legacyWorld=migrated.levels.at("LEVEL/A.3DG").world;const auto& motion=legacyWorld.motion;check(legacyWorld.difficulty==1,"Version 1 defaults to normal difficulty");
 check(legacyWorld.catalogHash==789&&motion.direction==17&&motion.turnSeconds==.003f,"Version 1 preserves catalog and turn state");
 check(std::abs(motion.velocityX-.46957427f)<.000001f&&std::abs(motion.velocityZ+1.40872283f)<.000001f&&motion.thrust==0&&motion.magnitude==std::hypot(motion.velocityX,motion.velocityZ),"Version 1 screen velocity migrates into native per-14-ms motion");
 std::vector<uint8_t> upgraded;check(encodeSave(migrated,upgraded,migrationError)&&upgraded[8]==35,"Migrated saves are rewritten as version 35");
 SaveGame upgradedRead;check(decodeSave(upgraded,upgradedRead,migrationError)&&upgradedRead.levels.at("LEVEL/A.3DG").world.motion.velocityZ==motion.velocityZ,"Migrated native velocity survives version 3 round trip");
 // V15 adds absent ending source (1 byte) and countdown (8 bytes) before
 // this fixture's empty renderer (16 bytes) and absent retry (1 byte).
 auto version28=upgraded;strip31WorldFields(version28,migrated);version28.erase(version28.end()-33,version28.end()-17);version28[8]=28;
 for(size_t i=0;i<8;++i)version28[12+i]=uint8_t((version28.size()-28)>>(i*8));reseal(version28);
 SaveGame migrated28;check(decodeSave(version28,migrated28,migrationError)&&migrated28.levels.begin()->second.world.nativeWorldRandom.seed==0xfeabcefeu,"Version28 defaults absent native idle RNG context");
 auto version15=version28;version15.erase(version15.begin()+28+8+migrated.currentLevel.size()+8+8+migrated.currentLevel.size());version15.erase(version15.end()-33,version15.end()-17);version15[8]=15;
 for(size_t i=0;i<8;++i)version15[12+i]=uint8_t((version15.size()-28)>>(i*8));reseal(version15);
 SaveGame migrated15;check(decodeSave(version15,migrated15,migrationError)&&migrated15.levels.begin()->second.world.ammoHits==0&&migrated15.levels.begin()->second.world.ammoMisses==0,"Version 15 defaults unavailable contact statistics to zero");
 auto version14=version15;version14.erase(version14.end()-26,version14.end()-17);version14[8]=14;
 for(size_t i=0;i<8;++i)version14[12+i]=uint8_t((version14.size()-28)>>(i*8));reseal(version14);
 SaveGame migrated14;check(decodeSave(version14,migrated14,migrationError)&&!migrated14.levels.begin()->second.world.timedProtectionKnown&&!migrated14.levels.begin()->second.world.endingSource&&migrated14.levels.begin()->second.world.endingRemainingMs==0,"Version 14 leaves protection unknown and does not invent an ending");
 // V13/V14 change only actor payloads, so this empty-actor fixture retains V12 layout.
 auto version12=version14;version12[8]=12;
 SaveGame migrated12;check(decodeSave(version12,migrated12,migrationError)&&migrated12.levels.begin()->second.world.heldItemsKnown,"Version 12 retained inventory loads");
 // This fixture has one empty renderer (two 8-byte fields). Strip the V12
 // held-item vector, then retry/difficulty fields to exercise prior layouts.
 auto version11=version14;version11.erase(version11.end()-25,version11.end()-17);version11[8]=11;
 for(size_t i=0;i<8;++i)version11[12+i]=uint8_t((version11.size()-28)>>(i*8));reseal(version11);
 SaveGame migrated11;check(decodeSave(version11,migrated11,migrationError)&&!migrated11.levels.begin()->second.world.heldItemsKnown,"Version 11 retains unknown legacy held inventory");
 check(!legacyWorld.heldItemsKnown&&upgradedRead.levels.begin()->second.world.heldItemsKnown,"Held inventory provenance follows disk version");
 auto version3=version11;version3.pop_back();version3[8]=3;
 for(size_t i=0;i<8;++i)version3[12+i]=uint8_t((version3.size()-28)>>(i*8));reseal(version3);
 SaveGame migrated3;check(decodeSave(version3,migrated3,migrationError)&&!migrated3.restartLevel,"Version 3 loads without invented retry history");
 auto version2=version3;version2.erase(version2.end()-24,version2.end()-16);version2[8]=2;
 for(size_t i=0;i<8;++i)version2[12+i]=uint8_t((version2.size()-28)>>(i*8));reseal(version2);
 SaveGame migrated2;check(decodeSave(version2,migrated2,migrationError),migrationError.c_str());
 check(migrated2.levels.at("LEVEL/A.3DG").world.difficulty==1&&migrated2.levels.at("LEVEL/A.3DG").world.motion.velocityZ==motion.velocityZ,"Version 2 preserves native motion and defaults difficulty to normal");
 // Captured from the unmodified version17 writer, with a real weapon state
 // and a live projectile. New slope fields must migrate without shifting data.
 const std::string slopeLegacy17Hex="5659525a5341564511000000fa04000000000000055addcb1a5a09d00b000000000000004c4556454c2f412e33444701000000000000000b000000000000004c4556454c2f412e334447000000000000000001000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000200000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000100000000000000010000000000000001000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000080bf0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000040000000000000000000000000000000000000000000000000000000000007a43000000000000b4430000000000000000000000000000000000000000000000000000000000000000000000000000000000000000ffffffffffffffff0000000000000000000000000000000000000000000000000000000000ffffffffffffffff01000000000000000a000000000000004761746c696e672d49490c000000000000006761746c696e672d616d6d6f0000000000000000000000000000000000000001d2040000000000000100000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000080bf00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000010000000000000000002842000000000000644200000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000803f000000000000803f00000000ec51783f00000000ec51783f00000000cdcccc3d00000000cdcccc3d00000000000000000000000000000000000000000000000000000000000000000000000000000000803f00000000000000000000000000000000000000000000000000000000000000803f0000000000000000000000000000803f00000000000000000000000000000000000000000000e04100000000000080bf000000000000000000000000000000000000000000010000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000001000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000";
 std::vector<uint8_t> slopeLegacy17;for(size_t i=0;i<slopeLegacy17Hex.size();i+=2)slopeLegacy17.push_back(uint8_t(std::stoul(slopeLegacy17Hex.substr(i,2),nullptr,16)));
 SaveGame slopeMigrated17;check(decodeSave(slopeLegacy17,slopeMigrated17,migrationError),migrationError.c_str());
 const auto& slopeWorld17=slopeMigrated17.levels.at("LEVEL/A.3DG").world;
 check(slopeWorld17.actors[0].weapons[0].state.lastFireMs==1234&&slopeWorld17.actors[0].weapons[0].state.alternateLeft&&slopeWorld17.projectiles[0].ageMs==28&&slopeWorld17.projectiles[0].position.x==42,"Version17 weapon/projectile payload retained");
 check(slopeWorld17.actors[0].weapons[0].state.slopeRadians==0&&!slopeWorld17.actors[0].weapons[0].state.slopeHeld&&!slopeWorld17.projectiles[0].slopeEnabled&&slopeWorld17.projectiles[0].slopeRadians==0&&slopeWorld17.projectiles[0].slopeHeight==0&&slopeWorld17.projectiles[0].slopeAltitude==0,"Version17 does not invent held slope or descending projectiles");
 // Genuine v18 writer output captured before the v19 serializer edit.
 const char* missile18Hex="5659525a53415645120000001c0500000000000036b582b48ce7cbc70b000000000000004c4556454c2f412e33444701000000000000000b000000000000004c4556454c2f412e334447000000000000000001000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000200000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000100000000000000010000000000000001000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000080bf0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000040000000000000000000000000000000000000000000000000000000000007a43000000000000b4430000000000000000000000000000000000000000000000000000000000000000000000000000000000000000ffffffffffffffff0000000000000000000000000000000000000000000000000000000000ffffffffffffffff01000000000000000a000000000000004761746c696e672d49490c000000000000006761746c696e672d616d6d6f0000000000000000000000000000000000000001d2040000000000000100000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000080bf00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000010000000000000000002842000000000000644200000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000803f000000000000803f00000000ec51783f00000000ec51783f00000000cdcccc3d00000000cdcccc3d00000000000000000000000000000000000000000000000000000000000000000000000000000000803f00000000000000000000000000000000000000000000000000000000000000803f0000000000000000000000000000803f00000000000000000000000000000000000000000000e04100000000000080bf0000000000000000000000000000000000000000000100000000000000000000000000000000000000000000000000000000000000000000000000000000000001000080be00000000000048410000000000004041000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000001000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000";
 std::vector<uint8_t> missile18;for(size_t i=0;missile18Hex[i];i+=2)missile18.push_back(uint8_t(std::stoul(std::string(missile18Hex+i,2),nullptr,16)));
 SaveGame missile18Read;check(decodeSave(missile18,missile18Read,migrationError),migrationError.c_str());
 const auto& legacyShot=missile18Read.levels.at("LEVEL/A.3DG").world.projectiles.at(0);
 check(legacyShot.position.x==42&&legacyShot.ageMs==28&&legacyShot.slopeEnabled&&legacyShot.slopeHeight==12.5f,"Genuine v18 populated shot retains trajectory and slope");
 check(!legacyShot.guidanceClass&&!legacyShot.guidanceTarget&&legacyShot.guidanceRoute.empty()&&!legacyShot.guidanceSearch&&!legacyShot.guidanceNative&&legacyShot.guidance.phase==MissileGuidancePhase::Idle,"Old in-flight shots never acquire retroactive guidance");
 SaveGame original;original.currentLevel="LEVEL/CAVE/CAVE.3DG";auto& level=original.levels[original.currentLevel];auto& w=level.world;
 w.endingSource=0;w.endingRemainingMs=3210.5f;w.difficulty=3;w.catalogHash=789;w.randomState="123 456 generator snapshot";w.nativeWorldRandom={123,456};w.message="saved";w.elapsedMs=750.5;w.playerKills=12;w.ammoHits=37;w.ammoMisses=11;w.shotsFired=23;w.pickupsTaken=4;w.actorsDestroyed=5;w.motion={17,.003f,12,-3,std::hypot(12.f,-3.f),4.5f};
 ObjectPlacement placement;placement.id={0};placement.classId={3};placement.position={100,200};placement.flags.zShadow=true;placement.sprite={"COMMON/LIFT.GAF",{1}};placement.owner=ObjectId{0};placement.properties.push_back({TextKind::Assignment,"Present",{}, {"1"},{},"test",8});placement.authored={TextKind::Block,"Object",{}, {},{},"test",1};w.objects.push_back(placement);
 ActorState actor;actor.spiderPatrolTurning=true;actor.enemyMoveCommand=208;actor.enemyMovedLastStep=true;actor.enemyThrust=7.25f;actor.enemyMagnitude=3.75f;actor.enemyMotionInitialized=true;actor.driveSegmentStart=ContentPosition{12,34};actor.driveSegmentEnd=ContentPosition{56,78};actor.contactVelocity={2.25f,-3.5f};actor.contactMotion=true;actor.timedProtection.reset({10000,2000});actor.timedProtection.advance(4321.5f);actor.enemy=true;actor.hudEnemy=true;actor.solid=true;actor.fireRevision=44;actor.ageMs=66.5;actor.gate=GateConfig{GateActive|GateOpened,50,9,{4}};actor.gateState={GateClosing,25,9,{5}};actor.gateRevision=7;actor.route={{120,200},{140,210}};actor.routePoint=1;actor.routeGoal=ContentPosition{140,210};actor.heldKeys={{9,true,2,3,"ABCD"}};actor.keys={9};actor.ammo["gun"]={42,100};EquippedWeapon gun;gun.name="gun";gun.ammo="gun";gun.state={730,true,-.375f,true};gun.config={33,2,true,true,true};actor.ammo.append("gun",{17,31});gun.ammoSlot=1;actor.weapons.push_back(gun);
 actor.lift=LiftConfig{LiftActive|LiftGoingUp,5000,3,{7}};actor.liftState={LiftActive|LiftPresent|LiftGoingUp,1234.5f,0,{7},true};
 actor.enemyTurn=EnemyTurnState{13,43,12,27,47,26,170,73,99,68,6,9,203,-11,true};
 actor.enemyTurn->gunLogicalHeading=13;actor.enemyTurn->gunCommand=203;
 actor.enemyTurn->gunPreviousCommand=205;actor.enemyTurn->gunInRange=true;actor.enemyTurn->gunStateKnown=true;
 actor.shieldUpStep=7.25f;
 ActorCreate actorCreate;actorCreate.classId={7};actorCreate.offset={3,4};actorCreate.nextMs=75.25f;actorCreate.repeatMs=250;actorCreate.trailStep=9;actorCreate.previousOrigin=ContentPosition{44,55};actorCreate.direction=17;actorCreate.done=true;actorCreate.currentPosition=true;actor.creates.push_back(actorCreate);actor.createsInitialized=true;
 Level terrain;terrain.mapW=20;terrain.mapH=20;terrain.hasTerrainCollision=true;actor.routeSearch.emplace(terrain,ContentPosition{64,64},ContentPosition{512,320},4);RouteBlocked blocked=[](ContentPosition a,ContentPosition b,float){return std::abs(a.x-b.x)>50||std::abs(a.y-b.y)>25;};check(actor.routeSearch->advance(terrain,blocked,1).status==RouteStatus::Pending,"Fixture leaves navigation pending");actor.trailSegment=FxTrailSegment{{11,22},8};w.actors.push_back(actor);
 Projectile p;p.slopeEnabled=true;p.slopeRadians=-.375f;p.slopeHeight=13.625f;p.slopeAltitude=13;p.position={90,40};p.velocity={3,2};p.visual=0;p.owner=0;p.classId={3};p.damage=17;p.fuseMs=83;p.activeAfterMs=100;p.ageMs=55;p.ignoreOwner=true;p.hitEffect="spark";p.motion={1,2,3,4};ProjectileCreate create;create.classId={3};create.trailStep=8;create.previousOrigin=ContentPosition{100,200};create.nextMs=90;create.repeatMs=1;p.creates.push_back(create);
 p.guidanceClass=ClassId{3};p.guidanceTarget=0;p.guidance.phase=MissileGuidancePhase::Searching;
 p.guidance.pose48=37;p.guidance.scanRemainingMs=-7;p.guidance.pathRemainingMs=123;p.guidance.turnRemainingMs=6;
 p.guidance.sampledTarget={150,75,4,5};p.guidanceRoute={{32,16},{64,32}};p.guidancePoint=1;p.guidanceStart={16,8};p.guidanceSearch=actor.routeSearch;
 w.projectiles.push_back(p);w.transition=TransitionRequest{0,"LEVEL/NEXT.3DG",125};w.rejectedLifts={0};w.savePlot=0;w.saveDwellMs=5500;w.saveAttempted=true;
 level.render.objectCount=1;level.render.channels.push_back({0,false,"COMMON/LIFT.GAF",1,{17,9,5.25},7,false,44,true});
 HeldItem held;held.object=placement;held.object.position={321,654};held.actor=actor;held.actor.ageMs=123.5;held.mapPath=original.currentLevel;held.className="retained-item";held.holder=0;w.heldItems.push_back(held);
 auto surpriseHeld=held;surpriseHeld.surprise=true;surpriseHeld.className="temporary-surprise";w.heldItems.push_back(surpriseHeld);
 original.levels["LEVEL/NEXT.3DG"]=level;
 level.render.effects.emplace().orbitTimeMs=812.5;
 std::vector<uint8_t> bytes;std::string error;check(encodeSave(original,bytes,error),error.c_str());
 {auto checkpoint=original;const auto& props=original.levels.begin()->second.world.objects[0].properties;
  const auto* shared=props.read().data();std::vector<uint8_t> encoded;
  check(checkpoint.levels.begin()->second.world.objects[0].properties.read().data()==shared,"Save copy shares original properties");
  check(encodeSave(checkpoint,encoded,error)&&encoded==bytes,"Shared property encoder retains exact bytes");
  check(props.read().data()==shared&&checkpoint.levels.begin()->second.world.objects[0].properties.read().data()==shared,"Encoding never detaches or mutates shared definitions");
 }
{auto owned=original;std::vector<uint8_t> movedBytes;check(encodeSave(std::move(owned),movedBytes,error)&&movedBytes==bytes,"Owned encoder preserves exact v35 bytes");}
 SaveGame decoded;check(decodeSave(bytes,decoded,error),error.c_str());std::vector<uint8_t> again;check(encodeSave(decoded,again,error)&&bytes==again,"Round trip retains every serialized field and navigation heap order");auto& restored=decoded.levels.at(original.currentLevel);check(!restored.render.effects,"Native-style disk load drops transient live renderer effects");check(restored.world.projectiles[0].fuseMs==83&&restored.world.actors[0].gateState.flags==GateClosing&&restored.render.channels[0].cursor.remaining==5.25,"Pending projectile, gate and animation state survives");check(bytes[8]==35&&restored.world.motion.thrust==4.5f&&restored.world.motion.magnitude==w.motion.magnitude&&restored.world.motion.velocityZ==-3,"Version35 retains native magnitude, thrust and both velocity axes");check(restored.world.difficulty==3,"Difficulty survives version 3 round trip");check(restored.world.savePlot==0&&restored.world.saveDwellMs==5500&&restored.world.saveAttempted,"Save terminal dwell and attempt latch survive");
 for(const auto* savedActor : {&restored.world.actors[0],&restored.world.heldItems[0].actor})
  check(savedActor->spiderPatrolTurning&&savedActor->enemyMoveCommand==208&&savedActor->enemyMovedLastStep&&savedActor->enemyMotionInitialized&&savedActor->enemyThrust==7.25f&&savedActor->enemyMagnitude==3.75f&&savedActor->driveSegmentStart->x==12&&savedActor->driveSegmentEnd->y==78,"World and held enemy propulsion state survives version20");
 for(const auto* savedActor : {&restored.world.actors[0],&restored.world.heldItems[0].actor})
  check(savedActor->contactVelocity.x==2.25f&&savedActor->contactVelocity.z==-3.5f&&savedActor->contactMotion,"World and retained actor native momentum/latch survive version 17");
 for(const auto* savedActor : {&restored.world.actors[0],&restored.world.heldItems[0].actor})
  check(savedActor->weapons[0].state.slopeRadians==-.375f&&savedActor->weapons[0].state.slopeHeld,"World and held weapon slope state survives version18");
 for(const auto* savedActor : {&restored.world.actors[0],&restored.world.heldItems[0].actor})
  check(savedActor->enemyTurn&&turnMarker(*savedActor->enemyTurn)==turnMarker(*actor.enemyTurn),
        "World and retained enemy body/gun poses, timers and lean survive version28");
 check(restored.world.nativeWorldRandom.seed==123&&restored.world.nativeWorldRandom.counter==456,"Native world idle RNG context survives version29");
 for(const auto* savedActor:{&restored.world.actors[0],&restored.world.heldItems[0].actor})
  check(savedActor->shieldUpStep==7.25f && savedActor->enemyTurn->gunLogicalHeading==13 &&
        savedActor->enemyTurn->gunCommand==203 && savedActor->enemyTurn->gunPreviousCommand==205 &&
        savedActor->enemyTurn->gunInRange && savedActor->enemyTurn->gunStateKnown,"v30 retains runtime shield and independent gun state in world/held copies");
 check(restored.world.projectiles[0].slopeEnabled&&restored.world.projectiles[0].slopeRadians==-.375f&&restored.world.projectiles[0].slopeHeight==13.625f&&restored.world.projectiles[0].slopeAltitude==13,"In-flight slope accumulator and integer height survive version18");
 check(restored.world.ammoHits==37&&restored.world.ammoMisses==11,"Ammo contact statistics survive version 16");
 check(restored.world.timedProtectionKnown&&restored.world.endingSource==0&&restored.world.endingRemainingMs==3210.5f,"Pending final-object ending survives version 16");
 for(const auto* savedActor : {&restored.world.actors[0],&restored.world.heldItems[0].actor})
  check(savedActor->timedProtection.active&&savedActor->timedProtection.onRemainingMs==5678.5f&&savedActor->timedProtection.offRemainingMs==2000,"World and retained actor protection phase survives version 16");
 check(restored.world.actors[0].trailSegment && restored.world.actors[0].trailSegment->previous.x==11 && restored.world.actors[0].trailSegment->step==8 &&
       restored.world.projectiles[0].creates[0].previousOrigin->y==200 && restored.world.projectiles[0].creates[0].trailStep==8,"FX segment endpoints and CREATE trail history survive version7");
 check(restored.world.actors[0].ammo.size()==2 && restored.world.actors[0].ammo.slots[0].second.count==42 &&
       restored.world.actors[0].ammo.slots[1].second.count==17 && restored.world.actors[0].ammo.slots[1].second.maximum==31 &&
       restored.world.actors[0].weapons[0].ammoSlot==1,"Duplicate ammo records and selected slot survive disk round trip");
 check(restored.world.heldItemsKnown&&restored.world.heldItems.size()==1&&restored.world.heldItems[0].object.position.x==321&&restored.world.heldItems[0].actor.ageMs==123.5&&restored.world.heldItems[0].actor.ammo.slots[1].second.count==17&&restored.world.heldItems[0].mapPath==original.currentLevel&&restored.world.heldItems[0].className=="retained-item"&&restored.world.heldItems[0].holder==0,"Retained item preserves object, actor and provenance while temporary Surprise is removed");
 for(const auto* savedActor : {&restored.world.actors[0],&restored.world.heldItems[0].actor}) {
  check(savedActor->createsInitialized&&savedActor->creates.size()==1,"World and retained actors preserve initialized CREATE lists");
  const auto& c=savedActor->creates[0];check(c.classId.value==7&&c.offset.x==3&&c.offset.y==4&&c.nextMs==75.25f&&c.repeatMs==250&&c.trailStep==9&&c.previousOrigin&&c.previousOrigin->x==44&&c.previousOrigin->y==55&&c.direction==17&&c.done&&c.currentPosition,"Actor CREATE timing, trail history and flags survive version 13");
 }
 check(restored.world.actors[0].lift&&restored.world.actors[0].lift->keyId==3&&restored.world.actors[0].liftState.keyId==0&&restored.world.actors[0].liftState.remainingMs==1234.5f&&restored.world.actors[0].liftState.passable&&restored.world.actors[0].gateRevision==7,"Lift countdown, spent key and animation revision survive disk continuation");
 check(w.heldItems.size()==2&&w.heldItems[1].surprise,"Disk normalization leaves live inventory unchanged");
 auto withRetry=original;withRetry.restartLevel=level;withRetry.restartLevel->world.transition.reset();
 withRetry.restartLevel->world.actors[0].ammo["bullet"]={321,1000};
 std::vector<uint8_t> retryBytes;SaveGame retryRead;
 check(encodeSave(withRetry,retryBytes,error)&&decodeSave(retryBytes,retryRead,error),"Retry checkpoint round trip");
 check(retryRead.restartLevel&&retryRead.restartLevel->world.actors[0].ammo.at("bullet").count==321,"Retry ammo is independent of current inventory");
 check(retryRead.restartLevel->world.actors[0].enemyTurn&&turnMarker(*retryRead.restartLevel->world.actors[0].enemyTurn)==turnMarker(*actor.enemyTurn),"Retry checkpoint preserves enemy turn continuation");
 withRetry.restartLevel->world.catalogHash^=1;
 check(!encodeSave(withRetry,retryBytes,error),"Mismatched retry level rejected");
 auto before=actor.routeSearch->advance(terrain,blocked,1000),after=restored.world.actors[0].routeSearch->advance(terrain,blocked,1000);check(before.status==after.status&&before.expanded==after.expanded&&before.points.size()==after.points.size(),"Restored search resumes same pending work");
 // Guidance policy, route progress and incremental-search heap resume exactly.
 const auto& savedGuidance=restored.world.projectiles[0];
 check(savedGuidance.guidanceClass==p.guidanceClass&&savedGuidance.guidanceTarget==p.guidanceTarget&&savedGuidance.guidance.pose48==37&&savedGuidance.guidance.scanRemainingMs==-7&&savedGuidance.guidance.pathRemainingMs==123&&savedGuidance.guidance.turnRemainingMs==6&&savedGuidance.guidance.sampledTarget.tileY==5&&savedGuidance.guidancePoint==1&&savedGuidance.guidanceStart.y==8&&savedGuidance.guidanceRoute[1].x==64,"Guidance state and route cursor survive v19");
 auto originalSearch=*p.guidanceSearch;auto resumedSearch=*savedGuidance.guidanceSearch;
 for(int tick=0;tick<1000;++tick){
  const auto left=originalSearch.advance(terrain,blocked,1),right=resumedSearch.advance(terrain,blocked,1);
  check(left.status==right.status&&left.expanded==right.expanded&&left.points.size()==right.points.size(),"Guidance search continuation matches each bounded step");
  for(size_t n=0;n<left.points.size();++n)check(left.points[n].x==right.points[n].x&&left.points[n].y==right.points[n].y,"Guidance search produces identical route");
  if(left.status!=RouteStatus::Pending)break;
 }
 for(int invalid=0;invalid<7;++invalid){
  auto badGuidance=original;auto& shot=badGuidance.levels.at(original.currentLevel).world.projectiles[0];
  if(invalid==0)shot.guidance.pose48=49;
  if(invalid==1)shot.guidance.phase=static_cast<MissileGuidancePhase>(99);
  if(invalid==2)shot.guidancePoint=shot.guidanceRoute.size()+1;
  if(invalid==3)shot.guidance.sampledTarget.x=std::numeric_limits<float>::quiet_NaN();
  if(invalid==4)shot.guidanceTarget=999;
  if(invalid==5)shot.guidanceSearch=NavigationSearch{};
  if(invalid==6)shot.guidance.scanRemainingMs=std::numeric_limits<int>::min();
  auto untouched=bytes;check(!encodeSave(badGuidance,untouched,error)&&untouched==bytes,"Invalid guidance is rejected without publishing bytes");
 }
 // Corrupt only the saved guidance pose, preserving container length/checksum.
 std::vector<uint8_t> guidanceMarker;
 for(int64_t n:{int64_t(MissileGuidancePhase::Searching),int64_t(-7),int64_t(123),int64_t(6),int64_t(37)})
  for(unsigned byte=0;byte<8;++byte)guidanceMarker.push_back(uint8_t(uint64_t(n)>>(8*byte)));
 auto corruptGuidance=bytes;auto guidanceAt=std::search(corruptGuidance.begin(),corruptGuidance.end(),guidanceMarker.begin(),guidanceMarker.end());
 check(guidanceAt!=corruptGuidance.end(),"Locate populated guidance state in disk fixture");
 *(guidanceAt+32)=49;reseal(corruptGuidance);SaveGame guidanceSentinel;guidanceSentinel.currentLevel="sentinel";
 check(!decodeSave(corruptGuidance,guidanceSentinel,error)&&guidanceSentinel.currentLevel=="sentinel","Corrupt disk guidance rejected transactionally after valid checksum");
 // Version28 preserves the independently advancing body/gun state, but rejects
 // malformed portable state before publishing a file or replacing a live save.
 for(bool heldActor:{false,true})for(int fault=0;fault<19;++fault){
  auto invalid=original;auto& world=invalid.levels.at(original.currentLevel).world;
  auto& turn=*(heldActor?world.heldItems[0].actor.enemyTurn:world.actors[0].enemyTurn);
  int* poses[]={&turn.bodyPose,&turn.bodyDesiredPose,&turn.bodyPreviousPose,&turn.gunPose,&turn.gunDesiredPose,&turn.gunPreviousPose};
  if(fault<12)*poses[fault/2]=fault%2?49:0;
  if(fault==12)turn.logicalHeading=24;
  if(fault==13)turn.orthogonalHeading=-1;
  if(fault==14)turn.turnCommand=200;
  if(fault==15)turn.lean=16;
  if(fault==16)turn.bodyPeriodMs=std::numeric_limits<int>::min();
  if(fault==17)turn.gunRemainingMs=-100000001;
  if(fault==18)turn.bodyRemainingMs=std::numeric_limits<int>::max();
  auto previous=bytes;check(!encodeSave(invalid,previous,error)&&previous==bytes,"Invalid world/held turn state rejected transactionally");
 }
 const auto turnBytes=turnMarker(*actor.enemyTurn);
 for(size_t field:{size_t(0),size_t(10),size_t(12),size_t(13),size_t(14)}){
  auto corrupt=bytes;const auto at=std::search(corrupt.begin(),corrupt.end(),turnBytes.begin(),turnBytes.end());
  check(at!=corrupt.end(),"Locate populated enemy turn state");
  if(field==14)*(at+113)=2; // Invalid bool after all fourteen fixed-width integers.
  else {const uint64_t value=field==0?49:field==10?24:field==12?200:16;
   for(unsigned byte=0;byte<8;++byte)*(at+1+field*8+byte)=uint8_t(value>>(byte*8));}
  reseal(corrupt);SaveGame sentinel;sentinel.currentLevel="sentinel";
  check(!decodeSave(corrupt,sentinel,error)&&sentinel.currentLevel=="sentinel","Corrupt enemy turn state rejected after valid checksum");
 }
 // v35 preserves six independent square floats, including accumulated
 // corner-rounding history that cannot be reconstructed from one center/radius.
 {auto source=original;source.levels.erase("LEVEL/NEXT.3DG");auto& world=source.levels.begin()->second.world;
  world.actors[0].nativeMotion=SpiderMotionHistory{{13579.5f,-24680.25f},{777,888}};
  world.actors[0].nativeGeneratorSquare=NativeGeneratorSquareState{123456.25f,-54321.75f,123400.125f,-54400.5f,123500.875f,-54200.25f};
  world.heldItems[0].actor.nativeGeneratorSquare=NativeGeneratorSquareState{23456.125f,34567.25f,23400.5f,34500.75f,23500.875f,34600.125f};
  source.restartLevel=source.levels.begin()->second;source.restartLevel->world.transition.reset();source.restartLevel->world.actors[0].nativeGeneratorSquare=NativeGeneratorSquareState{456.25f,789.5f,450.125f,780.25f,460.875f,790.75f};
  std::vector<uint8_t> packet;SaveGame read;check(encodeSave(source,packet,error)&&decodeSave(packet,read,error),"v35 native square actor/held encode/decode");check(packet[8]==35,"Native square uses v35 writer schema");
  const auto same=[](const NativeGeneratorSquareState&a,const NativeGeneratorSquareState&b){return squareMarker(a)==squareMarker(b);};
  const auto& result=read.levels.begin()->second.world;
  check(result.actors[0].nativeGeneratorSquare&&result.heldItems[0].actor.nativeGeneratorSquare&&same(*world.actors[0].nativeGeneratorSquare,*result.actors[0].nativeGeneratorSquare)&&same(*world.heldItems[0].actor.nativeGeneratorSquare,*result.heldItems[0].actor.nativeGeneratorSquare),"All six square fields retain exact bits and native serialized order for actors and held bodies");
  check(result.actors[0].nativeMotion->center.x==13579.5f&&result.actors[0].nativeMotion->previousSurface.y==888,"Square insertion retains adjacent native contact history");
  check(read.restartLevel&&read.restartLevel->world.actors[0].nativeGeneratorSquare&&same(*source.restartLevel->world.actors[0].nativeGeneratorSquare,*read.restartLevel->world.actors[0].nativeGeneratorSquare),"Retry checkpoint preserves independent native square history");
  const auto marker=squareMarker(world.actors[0].nativeGeneratorSquare);
  for(int fault=0;fault<7;++fault){auto bad=source;auto& square=*bad.levels.begin()->second.world.actors[0].nativeGeneratorSquare;
   if(fault==0)square.centerX=std::numeric_limits<float>::quiet_NaN();if(fault==1)square.centerZ=std::numeric_limits<float>::infinity();
   if(fault==2)square.minX=square.maxX+1;if(fault==3)square.minZ=square.maxZ+1;if(fault==4)square.maxX=square.minX-1;if(fault==5)square.maxZ=square.minZ-1;if(fault==6)square.minX=std::numeric_limits<float>::infinity();
   auto untouched=packet;check(!encodeSave(bad,untouched,error)&&untouched==packet,"Invalid square cannot replace encoded save bytes");}
  for(int fault=0;fault<5;++fault){auto corrupt=packet;const auto at=std::search(corrupt.begin()+28,corrupt.end(),marker.begin(),marker.end());check(at!=corrupt.end()&&std::search(at+1,corrupt.end(),marker.begin(),marker.end())==corrupt.end(),"Locate actual v35 populated square field order");
   if(fault==0)*at=2;else {const unsigned field=fault==1?0:fault==2?2:fault==3?3:5;const float value=fault==1?std::numeric_limits<float>::quiet_NaN():fault==2?123501.875f:fault==3?-54199.25f:-54401.5f;const uint64_t bits=std::bit_cast<uint32_t>(value);for(unsigned byte=0;byte<8;++byte)*(at+1+field*8+byte)=uint8_t(bits>>(byte*8));}
   reseal(corrupt);SaveGame sentinel;sentinel.currentLevel="sentinel";check(!decodeSave(corrupt,sentinel,error)&&sentinel.currentLevel=="sentinel","Corrupt optional/float/extrema order rejected transactionally after valid checksum");}
  auto old34=packet;strip35ActorFields(old34,source);check(decodeSave(old34,read,error),error.c_str());const auto& legacy=read.levels.begin()->second.world;
  check(!legacy.actors[0].nativeGeneratorSquare&&!legacy.heldItems[0].actor.nativeGeneratorSquare&&legacy.actors[0].nativeMotion->center.x==13579.5f&&legacy.actors[0].reactivateMs==world.actors[0].reactivateMs,"Actual v34 payload omits present/absent v35 square fields without shifting prior actor/held fields");
  check(read.restartLevel&&!read.restartLevel->world.actors[0].nativeGeneratorSquare&&!read.restartLevel->world.heldItems[0].actor.nativeGeneratorSquare,"Genuine v34 retry actor and held layouts omit v35 state independently");
  std::vector<uint8_t> rewritten;check(encodeSave(read,rewritten,error)&&rewritten[8]==35,"Genuine v34 reader rewrites v35 without inventing unknown square history");
 }
 // Remove V5 reactivation and V6/V7 absent Explosion/altitude/trail optionals from a
 // one-actor/no-projectile fixture to exercise the genuine V4 actor layout.
 auto v4Source=original;v4Source.levels.erase("LEVEL/NEXT.3DG");
 v4Source.levels.begin()->second.world.endingSource.reset();v4Source.levels.begin()->second.world.endingRemainingMs=0;
 v4Source.levels.begin()->second.world.projectiles.clear();
 v4Source.levels.begin()->second.world.heldItems.clear();
 v4Source.levels.begin()->second.render.channels.clear();
 v4Source.levels.begin()->second.world.actors[0].weapons.clear();
 v4Source.levels.begin()->second.world.actors[0].heldKeys.clear();
 v4Source.levels.begin()->second.world.actors[0].ammo.clear();
 v4Source.levels.begin()->second.world.actors[0].trailSegment.reset();
 v4Source.levels.begin()->second.world.actors[0].lift.reset();
 v4Source.levels.begin()->second.world.actors[0].creates.clear();
 v4Source.levels.begin()->second.world.actors[0].createsInitialized=false;
 v4Source.levels.begin()->second.world.actors[0].enemyTurn.reset();
 v4Source.levels.begin()->second.world.actors[0].shieldUpStep.reset();
 v4Source.levels.begin()->second.world.actors[0].reactivateMs=777.75f;
 std::vector<uint8_t> timerBytes;SaveGame timerRead;
 check(encodeSave(v4Source,timerBytes,error)&&decodeSave(timerBytes,timerRead,error),"Reactivation timer round trip");
 check(timerRead.levels.begin()->second.world.actors[0].reactivateMs==777.75f,"Pickup reactivation survives save");
 // Derive an actual v33 packet: remove only v34 fields from a populated
 // actor/projectile fixture, preserving v32 identities and v33 World tail.
 {
  auto source=v4Source;auto& world=source.levels.begin()->second.world;
  world.nativeGlobalRandom={33301,33302};world.nativeReplayRandom={34401,34402};
  auto& body=world.actors[0];body.nativeGeneration=33001;body.nativeCreatorGeneration=33002;
  body.nativeAmmoOwner={5,7,11,13};
  body.nativeGeneratorSquare=NativeGeneratorSquareState{30001.25f,30002.5f,29990,29991,30010,30011};
  world.projectiles={p};world.projectiles[0].nativeGeneration=33101;
  world.projectiles[0].nativeOwnerGeneration=33001;
  world.projectiles[0].visual.reset();world.projectiles[0].nativeBodyIdentity=33103;
  world.projectiles[0].nativeBareBody.emplace();auto& compact=*world.projectiles[0].nativeBareBody;
  compact.energy=500;compact.shield=1000;compact.nativeHit=true;
  compact.motion={{1234.25f,2345.5f},{3456.75f,4567.125f}};
  std::vector<uint8_t> packet;check(encodeSave(source,packet,error),"v33 migration fixture encode");
  strip34AbsentWorldTail(packet,source);
  std::vector<uint8_t> actorMarker(8);const auto timer=std::bit_cast<uint32_t>(777.75f);
  for(unsigned byte=0;byte<4;++byte)actorMarker[byte]=uint8_t(timer>>(8*byte));
  auto actorAt=std::search(packet.begin()+28,packet.end(),actorMarker.begin(),actorMarker.end());
  check(actorAt!=packet.end(),"Locate v33 actor fixture timer");
  // Timer+139 is v31's pending/count/absent-AI trio; v34 follows it.
  strip34AbsentActorFields(packet,size_t(actorAt-packet.begin())+142);
  std::vector<uint8_t> projectileMarker;
  for(uint64_t value:{UINT64_MAX,uint64_t(33101),uint64_t(33001),uint64_t(33103)})
   for(unsigned byte=0;byte<8;++byte)projectileMarker.push_back(uint8_t(value>>(8*byte)));
  auto projectileAt=std::search(packet.begin()+28,packet.end(),projectileMarker.begin(),projectileMarker.end());
  check(projectileAt!=packet.end()&&std::search(projectileAt+1,packet.end(),projectileMarker.begin(),projectileMarker.end())==packet.end(),
        "Locate unique v34 projectile pool word before v32 identity");
  packet.erase(projectileAt,projectileAt+8);
  std::vector<uint8_t> compactMarker;
  for(float value:{1234.25f,2345.5f})
   for(unsigned byte=0;byte<8;++byte)compactMarker.push_back(uint8_t(uint64_t(std::bit_cast<uint32_t>(value))>>(8*byte)));
  compactMarker.insert(compactMarker.end(),8,255); // NativeNavPoint.graphNode
  for(float value:{3456.75f,4567.125f})
   for(unsigned byte=0;byte<8;++byte)compactMarker.push_back(uint8_t(uint64_t(std::bit_cast<uint32_t>(value))>>(8*byte)));
  auto compactAt=std::search(packet.begin()+28,packet.end(),compactMarker.begin(),compactMarker.end());
  check(compactAt!=packet.end()&&compactAt[40]==1&&
        std::search(compactAt+1,packet.end(),compactMarker.begin(),compactMarker.end())==packet.end(),
        "Locate unique v34 compact-Ammo Hit field after retained motion");
  packet.erase(compactAt+40);packet[8]=33;
  for(unsigned byte=0;byte<8;++byte)packet[12+byte]=uint8_t((packet.size()-28)>>(8*byte));reseal(packet);
  SaveGame read;check(decodeSave(packet,read,error),error.c_str());const auto& old=read.levels.begin()->second.world;
  check(old.actors[0].nativeGeneration==33001&&old.actors[0].nativeCreatorGeneration==33002&&
        old.actors[0].nativeAmmoOwner.activeAmmo==13&&old.actors[0].nativeAmmoOwner.kills==11&&
        old.projectiles[0].nativeGeneration==33101&&old.projectiles[0].nativeOwnerGeneration==33001&&
        old.projectiles[0].nativeBodyIdentity==33103&&old.projectiles[0].nativeBareBody&&
        old.projectiles[0].nativeBareBody->energy==500&&old.projectiles[0].nativeBareBody->shield==1000&&
        old.projectiles[0].nativeBareBody->motion.center.x==1234.25f&&
        old.projectiles[0].nativeBareBody->motion.previousSurface.y==4567.125f&&
        old.projectiles[0].ageMs==55&&old.actors[0].reactivateMs==777.75f,
        "Actual v33 layout preserves actor/Ammo identities, owner counters and trajectory");
  check(old.nativeGlobalRandom.seed==33301&&old.nativeWorldRandom.seed==123&&
        !old.nativeAwake&&!old.nativePlayerFacing&&!old.nativePool&&
        old.nativePlayerPreviousSurface.x==0&&old.nativePlayerPreviousSurface.y==0&&
        old.nativeReplayRandom.seed==NativeRandomState{}.seed&&old.nativeReplayRandom.counter==NativeRandomState{}.counter&&
        !old.actors[0].nativeGeneratorSquare&&!old.actors[0].nativeHit&&!old.actors[0].nativeFireRequested&&old.actors[0].nativePoolSlot==SIZE_MAX&&
        old.projectiles[0].nativePoolSlot==SIZE_MAX&&!old.projectiles[0].nativeBareBody->nativeHit,
        "v33 migration preserves old RNG and leaves v34 manager/Hit/pool/replay state absent");
  check(encodeSave(read,packet,error)&&packet[8]==35,"v33 reader rewrites current v35 schema");
 }
 strip31WorldFields(timerBytes,v4Source);
 // Derive the actual one-actor V27 packet by removing V28's absent optional
 // at the actor tail, leaving every preceding byte and the following world intact.
 const uint32_t timerBits=std::bit_cast<uint32_t>(777.75f);std::vector<uint8_t> marker(8);
 for(size_t i=0;i<4;++i)marker[i]=uint8_t(timerBits>>(8*i));
 auto at=std::search(timerBytes.begin()+28,timerBytes.end(),marker.begin(),marker.end());
 check(at!=timerBytes.end()&&*(at+137)==0,"Locate absent V28 turn-state optional after contact momentum");
 check(*(at+138)==0,"Locate absent v30 shield step");
 strip31AbsentActorTail(timerBytes,std::distance(timerBytes.begin(),at)+139);
 auto actorVersion30=timerBytes;actorVersion30[8]=30;
 for(size_t i=0;i<8;++i)actorVersion30[12+i]=uint8_t((actorVersion30.size()-28)>>(i*8));reseal(actorVersion30);
 check(decodeSave(actorVersion30,timerRead,error)&&!timerRead.levels.begin()->second.world.actorCells&&
       !timerRead.levels.begin()->second.world.actors[0].nativeDamagePending&&
       !timerRead.levels.begin()->second.world.actors[0].nativeKillCounted&&
       !timerRead.levels.begin()->second.world.actors[0].ordinaryAI&&
       timerRead.levels.begin()->second.world.nativeWorldRandom.seed==123&&
       timerRead.levels.begin()->second.world.nativeGlobalRandom.seed==NativeRandomState{}.seed,
       "Actual v30 layout preserves old RNG and leaves v31 producer/death/AI unknown");
 // Remove V29's world RNG tail immediately before this empty renderer/retry.
 timerBytes.erase(timerBytes.end()-33,timerBytes.end()-17);
 timerBytes.erase(at+138);
 timerBytes.erase(at+137);timerBytes[8]=27;
 for(size_t i=0;i<8;++i)timerBytes[12+i]=uint8_t((timerBytes.size()-28)>>(8*i));reseal(timerBytes);
 check(decodeSave(timerBytes,timerRead,error)&&!timerRead.levels.begin()->second.world.actors[0].enemyTurn&&
       timerRead.levels.begin()->second.world.actors[0].reactivateMs==777.75f&&
       timerRead.levels.begin()->second.world.actors[0].contactVelocity.z==-3.5f,
       "Version27 retains existing actor data and leaves native turning unknown for runtime migration");
 timerBytes.erase(timerBytes.begin()+28+8+v4Source.currentLevel.size()+8+8+v4Source.currentLevel.size()); // absent v24 navigation graph
 at=std::search(timerBytes.begin()+28,timerBytes.end(),marker.begin(),marker.end());
 check(at!=timerBytes.end(),"Timer fixture marker found");
 // V17 appends two floating-point coordinates (8 bytes each on disk) and
 // the contact-motion latch after V15's protection phase. Retain a real V16
 // actor fixture and ensure older saves invent no stored collision momentum.
 timerBytes.erase(at+57); // absent v26 non-Spider contact history
 const auto spiderOffset=std::distance(timerBytes.begin(),at)+57;
 timerBytes.erase(timerBytes.begin()+spiderOffset); // absent v24 native AI
 const auto retreatOffset=std::distance(timerBytes.begin(),at)+57;
 timerBytes.erase(timerBytes.begin()+retreatOffset,timerBytes.begin()+retreatOffset+1);
 // Remove v21 command (8 bytes) and movement flag, then v20 state.
 const auto commandOffset=std::distance(timerBytes.begin(),at)+57;
 timerBytes.erase(timerBytes.begin()+commandOffset,timerBytes.begin()+commandOffset+9);
 // Remove v20 propulsion state first (this fixture retains both segment endpoints).
 const auto enemyOffset=std::distance(timerBytes.begin(),at)+57;
 timerBytes.erase(timerBytes.begin()+enemyOffset,timerBytes.begin()+enemyOffset+51);
 const auto contactOffset=enemyOffset;
 timerBytes.erase(timerBytes.begin()+contactOffset,timerBytes.begin()+contactOffset+17);
 auto actorVersion16=timerBytes;actorVersion16[8]=16;
 for(size_t i=0;i<8;++i)actorVersion16[12+i]=uint8_t((actorVersion16.size()-28)>>(8*i));reseal(actorVersion16);
 check(decodeSave(actorVersion16,timerRead,error)&&timerRead.levels.begin()->second.world.actors[0].contactVelocity.x==0&&timerRead.levels.begin()->second.world.actors[0].contactVelocity.z==0&&!timerRead.levels.begin()->second.world.actors[0].contactMotion,"Version 16 actors initialize zero contact momentum and latch");
 at=std::search(timerBytes.begin()+28,timerBytes.end(),marker.begin(),marker.end());
 // Remove V16 contact statistics; retain a real V15 fixture before older migrations.
 timerBytes.erase(timerBytes.end()-33,timerBytes.end()-17);
 auto actorVersion15=timerBytes;actorVersion15[8]=15;
 for(size_t i=0;i<8;++i)actorVersion15[12+i]=uint8_t((actorVersion15.size()-28)>>(8*i));reseal(actorVersion15);
 check(decodeSave(actorVersion15,timerRead,error)&&timerRead.levels.begin()->second.world.ammoHits==0&&timerRead.levels.begin()->second.world.actors[0].timedProtection.active,"Version 15 preserves protection and defaults statistics");
 // Remove V15 world ending fields and per-actor two counters/active flag.
 timerBytes.erase(timerBytes.end()-26,timerBytes.end()-17);
 timerBytes.erase(at+40,at+57);
 auto actorVersion14=timerBytes;actorVersion14[8]=14;
 for(size_t i=0;i<8;++i)actorVersion14[12+i]=uint8_t((actorVersion14.size()-28)>>(8*i));reseal(actorVersion14);
 check(decodeSave(actorVersion14,timerRead,error)&&!timerRead.levels.begin()->second.world.timedProtectionKnown&&!timerRead.levels.begin()->second.world.actors[0].timedProtection.active&&timerRead.levels.begin()->second.world.actors[0].timedProtection.onRemainingMs==0,"Version 14 actor protection awaits authored initialization");
 // Strip V14's absent Lift marker, retaining a real V13 actor layout.
 timerBytes.erase(at+39);
 auto actorVersion13=timerBytes;actorVersion13[8]=13;
 for(size_t i=0;i<8;++i)actorVersion13[12+i]=uint8_t((actorVersion13.size()-28)>>(8*i));reseal(actorVersion13);
 check(decodeSave(actorVersion13,timerRead,error)&&!timerRead.levels.begin()->second.world.actors[0].lift,"Version 13 actor leaves Lift cycle unknown for authored migration");
 // Strip V13's empty CREATE vector and initialized flag at the end of the actor.
 timerBytes.erase(at+30,at+39);
 auto actorVersion12=timerBytes;actorVersion12[8]=12;
 for(size_t i=0;i<8;++i)actorVersion12[12+i]=uint8_t((actorVersion12.size()-28)>>(8*i));reseal(actorVersion12);
 check(decodeSave(actorVersion12,timerRead,error)&&timerRead.levels.begin()->second.world.actors[0].creates.empty()&&!timerRead.levels.begin()->second.world.actors[0].createsInitialized,"Version 12 actors defer CREATE initialization to authored definitions");
 // Strip the V12 empty retained-item vector before deriving older layouts.
 timerBytes.erase(timerBytes.end()-25,timerBytes.end()-17);
 at=std::search(timerBytes.begin()+28,timerBytes.end(),marker.begin(),marker.end());
 // Exercise each intervening reader with the actual absent-field layout.
 auto verifyLegacy=[&](std::vector<uint8_t> fixture,uint8_t version){fixture[8]=version;for(size_t i=0;i<8;++i)fixture[12+i]=uint8_t((fixture.size()-28)>>(8*i));reseal(fixture);check(decodeSave(fixture,timerRead,error),error.c_str());};
 verifyLegacy(timerBytes,11);
 const auto timerOffset=std::distance(timerBytes.begin(),at);
 auto actorVersion10=timerBytes;actorVersion10.erase(actorVersion10.begin()+timerOffset+11,actorVersion10.begin()+timerOffset+20);verifyLegacy(actorVersion10,10);
 auto actorVersion9=actorVersion10;actorVersion9.erase(actorVersion9.begin()+timerOffset+11,actorVersion9.begin()+timerOffset+21);verifyLegacy(actorVersion9,9);
 auto actorVersion8=actorVersion9;actorVersion8.erase(actorVersion8.begin()+timerOffset-1);verifyLegacy(actorVersion8,8);
 // No weapons/ammo/key records here, so V7 and V8 have the same payload.
 verifyLegacy(actorVersion8,7);
 auto actorVersion5=actorVersion9;actorVersion5.erase(actorVersion5.begin()+timerOffset+8,actorVersion5.begin()+timerOffset+11);actorVersion5.erase(actorVersion5.begin()+timerOffset-1);verifyLegacy(actorVersion5,5);
 // Strip V11 pool marker/revision and V10 empty generator vector/parent/flag.
 timerBytes.erase(at+11,at+30);
 at=std::search(timerBytes.begin()+28,timerBytes.end(),marker.begin(),marker.end());
 auto v6Bytes=timerBytes;v6Bytes.erase(v6Bytes.begin()+std::distance(timerBytes.begin(),at)+10);v6Bytes.erase(v6Bytes.begin()+std::distance(timerBytes.begin(),at)-1);v6Bytes[8]=6;
 for(size_t i=0;i<8;++i)v6Bytes[12+i]=uint8_t((v6Bytes.size()-28)>>(8*i));reseal(v6Bytes);
 check(decodeSave(v6Bytes,timerRead,error)&&timerRead.levels.begin()->second.world.actors[0].reactivateMs==777.75f &&
       !timerRead.levels.begin()->second.world.actors[0].trailSegment,"V6 actor loads without invented FX trail");
 timerBytes.erase(at-1,at+11);timerBytes[8]=4;
 for(size_t i=0;i<8;++i)timerBytes[12+i]=uint8_t((timerBytes.size()-28)>>(8*i));reseal(timerBytes);
 check(decodeSave(timerBytes,timerRead,error)&&timerRead.levels.begin()->second.world.actors[0].reactivateMs==-1,"V4 actors load without invented reactivation");
 SaveGame unchanged;unchanged.currentLevel="sentinel";auto bad=bytes;bad.back()^=1;check(!decodeSave(bad,unchanged,error)&&unchanged.currentLevel=="sentinel","Corruption rejected transactionally");bad=bytes;bad[8]=99;check(!decodeSave(bad,unchanged,error),"Unknown version rejected");bad=bytes;bad.push_back(0);check(!decodeSave(bad,unchanged,error),"Trailing data rejected");uint64_t payloadSize=bad.size()-28;for(size_t i=0;i<8;++i)bad[12+i]=uint8_t(payloadSize>>(i*8));reseal(bad);check(!decodeSave(bad,unchanged,error),"Trailing payload rejected even with matching length and checksum");for(size_t n: {size_t(0),size_t(7),size_t(27),bytes.size()-1})check(!decodeSave(std::span<const uint8_t>(bytes).first(n),unchanged,error),"Truncation rejected");
 bad=bytes;for(size_t i=28;i<36;++i)bad[i]=255;reseal(bad);check(!decodeSave(bad,unchanged,error),"Malicious string size rejected after valid checksum");
 bad=bytes;size_t objectIdOffset=28+8+original.currentLevel.size()+8+8+original.levels.begin()->first.size()+1+8+8;bad[objectIdOffset+5]=1;reseal(bad);check(!decodeSave(bad,unchanged,error),"Wide encoded integer outside uint32 range rejected");
 bad=bytes;size_t count=36+original.currentLevel.size();for(size_t i=count;i<count+8;++i)bad[i]=255;reseal(bad);check(!decodeSave(bad,unchanged,error),"Malicious collection count rejected");
 for(bool heldActor:{false,true})for(float nonfinite:{std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()}){
  auto invalidMomentum=original;auto& state=invalidMomentum.levels.begin()->second.world;
  auto& body=heldActor?state.heldItems[0].actor:state.actors[0];body.contactVelocity.z=nonfinite;auto previous=bytes;
  check(!encodeSave(invalidMomentum,previous,error)&&previous==bytes,"Nonfinite world/held contact momentum rejected transactionally");
 }
 for(int field=0;field<5;++field){auto invalidSlope=original;auto& state=invalidSlope.levels.begin()->second.world;float* value=field==0?&state.actors[0].weapons[0].state.slopeRadians:field==1?&state.heldItems[0].actor.weapons[0].state.slopeRadians:field==2?&state.projectiles[0].slopeRadians:field==3?&state.projectiles[0].slopeHeight:&state.projectiles[0].slopeAltitude;*value=std::numeric_limits<float>::quiet_NaN();auto previous=bytes;check(!encodeSave(invalidSlope,previous,error)&&previous==bytes,"Nonfinite weapon/projectile slope rejected transactionally");}
 auto invalidDifficulty=original;invalidDifficulty.levels.begin()->second.world.difficulty=4;std::vector<uint8_t> rejected;check(!encodeSave(invalidDifficulty,rejected,error),"Invalid difficulty cannot enter save");
 auto invalid=original;invalid.levels.begin()->second.world.motion.velocityX=std::numeric_limits<float>::quiet_NaN();auto output=bytes;check(!encodeSave(invalid,output,error)&&output==bytes,"Nonfinite values rejected without replacing output");invalid=original;invalid.currentLevel="../escape.3DG";check(!encodeSave(invalid,output,error),"Traversal path rejected");check(!validSaveLevelPath("/absolute.3DG")&&!validSaveLevelPath("C:/LEVEL.3DG")&&!validSaveLevelPath("LEVEL\\A.3DG")&&validSaveLevelPath("LEVEL/A.3dg"),"Level path restrictions");
 {
  auto native=original;auto& w=native.levels.begin()->second.world;
  w.spiderNavigation=NativeNavGraph{{{0,{12,34,12,34},true,true}},{{0,false}},{{0,false}}};
  w.actors[0].spiderAI.emplace();w.actors[0].spiderAI->patrolRoute.points={{12,34,0}};
  {auto copy=w.actors[0];copy.spiderAI->patrolRoute.points[0].x=999;
   check(w.actors[0].spiderAI->patrolRoute.points[0].x==12,"Snapshot Spider state owns an independent route");
   copy.spiderAI.reset();check(w.actors[0].spiderAI.has_value(),"Reset copied Spider state preserves original");}
  w.actors[0].spiderAI->motion=SpiderMotionHistory{{2538.49609375f,285.657684326171875f},{2015,1263}};
  w.actors[0].spiderAI->turn=initializeEnemyTurn({0x1f,false,true,42,0},0,0);
  w.actors[0].spiderAI->turn->bodyPose=2;w.actors[0].spiderAI->turn->bodyDesiredPose=13;w.actors[0].spiderAI->turn->bodyRemainingMs=26;
  w.actors[0].nativeMotion=SpiderMotionHistory{{987.25f,654.5f},{321.75f,123.25f}};
  std::vector<uint8_t> payload;check(encodeSave(native,payload,error),"Native graph and route encode");
  SaveGame restored;check(decodeSave(payload,restored,error),"Native graph and route decode");
  check(restored.levels.begin()->second.world.actors[0].spiderAI->patrolRoute.points[0].graphNode==0,"Native route identity roundtrip");
  check(restored.levels.begin()->second.world.actors[0].nativeMotion->center.x==987.25f && restored.levels.begin()->second.world.actors[0].nativeMotion->previousSurface.y==123.25f,"Ordinary enemy native contact history survives save26");
  check(restored.levels.begin()->second.world.actors[0].spiderAI->motion->previousSurface.x==2015 && restored.levels.begin()->second.world.actors[0].spiderAI->motion->center.x==2538.49609375f,"Spider dual coordinates and prior surface survive disk roundtrip");
  check(restored.levels.begin()->second.world.actors[0].spiderAI->turn->bodyPose==2&&restored.levels.begin()->second.world.actors[0].spiderAI->turn->bodyDesiredPose==13&&restored.levels.begin()->second.world.actors[0].spiderAI->turn->bodyRemainingMs==26&&restored.levels.begin()->second.world.actors[0].spiderAI->turn->logicalHeading==0,"Spider shared idle/route turn state survives version29");
  for(int fault=0;fault<6;++fault){auto bad=native;auto& world=bad.levels.begin()->second.world;
   if(fault==0)world.spiderNavigation->xEdges.push_back({0,false});
   if(fault==1)world.spiderNavigation->yEdges.clear();
   if(fault==2)world.actors[0].spiderAI->patrolRoute.points[0].graphNode=4;
   if(fault==3)world.actors[0].spiderAI->patrolRoute.points[0].x=99;
   if(fault==4)world.actors[0].spiderAI->motion->center.x=std::numeric_limits<float>::quiet_NaN();
   if(fault==5)world.actors[0].nativeMotion->previousSurface.y=std::numeric_limits<float>::infinity();
   auto unchanged=payload;check(!encodeSave(bad,unchanged,error)&&unchanged==payload,"Broken Spider graph rejected transactionally");
  }
 }
 // V31 preserves independent actor visitation order, deferred native Ammo
 // lethal state, immediate credit and ordinary enemy action/RNG continuation.
 {
  auto native=original;native.levels.erase("LEVEL/NEXT.3DG");auto& world=native.levels.begin()->second.world;
  world.nativeGlobalRandom={918273,564738};
  world.actorCells.emplace();world.actorCells->reset(20,20);
  world.actorCells->relocate(0,world.actorCells->coordinate(world.objects[0].position.x,world.objects[0].position.y));
  auto& body=world.actors[0];body.vitals.energy=-13.25f;body.nativeDamagePending=true;body.nativeKillCounted=true;
  body.ordinaryAI.emplace();auto& ai=*body.ordinaryAI;
  ai.patrol.initialized=true;ai.patrol.reverse=true;ai.patrol.point=2;ai.patrol.goal=ContentPosition{80,160};
  ai.patrol.goalCellX=1;ai.patrol.goalCellY=5;ai.random.reuseCount=3;ai.random.replayRandom={87654,32109};
  ai.actorFlags=0x8000000;ai.targeting.clock.scanRemainingMs=17;ai.base.clock.pathRemainingMs=19;
  {ActorState copy;copy=body;copy.ordinaryAI->targeting.clock.scanRemainingMs=999;
   check(ai.targeting.clock.scanRemainingMs==17,"Snapshot ordinary AI assignment owns independent state");
   auto moved=std::move(copy);check(moved.ordinaryAI->targeting.clock.scanRemainingMs==999,"Moving actor preserves owned AI state");}
  std::vector<uint8_t> packet,repeat;SaveGame read;
  check(encodeSave(native,packet,error)&&decodeSave(packet,read,error)&&encodeSave(read,repeat,error)&&packet==repeat,
        "v31 producer/pending/ordinary-AI state roundtrips exact bytes");
  const auto& saved=read.levels.begin()->second.world;const auto& result=saved.actors[0];
  check(saved.actorCells&&saved.actorCells->heads==world.actorCells->heads&&saved.actorCells->next==world.actorCells->next&&
        saved.actorCells->cell==world.actorCells->cell&&result.nativeDamagePending&&result.nativeKillCounted&&result.vitals.energy==-13.25f,
        "v31 preserves exact cell producer and deferred health/credit flags");
  check(saved.nativeGlobalRandom.seed==918273&&saved.nativeGlobalRandom.counter==564738&&result.ordinaryAI&&
        result.ordinaryAI->patrol.reverse&&result.ordinaryAI->patrol.point==2&&result.ordinaryAI->patrol.goal->y==160&&
        result.ordinaryAI->random.reuseCount==3&&result.ordinaryAI->random.replayRandom.counter==32109&&
        result.ordinaryAI->targeting.clock.scanRemainingMs==17&&result.ordinaryAI->base.clock.pathRemainingMs==19,
        "v31 preserves native outer RNG and ordinary action/patrol/replay clocks");
  for(int fault=0;fault<3;++fault){auto bad=native;auto& state=bad.levels.begin()->second.world;
   if(fault==0)state.actors[0].vitals.energy=1;
   if(fault==1)state.actorCells->next[0]=0;
   if(fault==2)state.actors[0].ordinaryAI->targeting.clock.scanRemainingMs=std::numeric_limits<int>::min();
   auto unchanged=packet;check(!encodeSave(bad,unchanged,error)&&unchanged==packet,
         "Invalid v31 pending/cell/action state rejected without replacing bytes");
  }
 }
 // V27 native missile searches and the saved old action retain ordered graph
 // identity, including abandoned branches still owned by a completed action.
 {
  auto native=original;native.levels.erase("LEVEL/NEXT.3DG");auto& world=native.levels.begin()->second.world;
  world.spiderNavigation.emplace();auto& graph=*world.spiderNavigation;
  const auto point=[&](float x,float y){const size_t id=graph.objects.size();
   graph.objects.push_back({0,{x,y,x,y},true,true});graph.xEdges.push_back({id,false});graph.yEdges.push_back({id,false});
   return NativeNavPoint{x,y,id};};
  const auto a=point(10,20),b=point(70,80),c=point(40,50);
  const auto d=point(11,21),e=point(41,51),f=point(71,81),unused=point(100,101);
  auto& shot=world.projectiles[0];shot.guidanceSearch.reset();shot.guidanceRoute.clear();shot.guidancePoint=0;
  shot.guidanceNative.emplace();auto& nav=*shot.guidanceNative;
  nav.search=NativeNavState{{{{a,c,b},1}},{b,a,c},{3,4},250};
  nav.saved.emplace();auto& backup=*nav.saved;backup.route.points={d,e,f};backup.route.ownedPoints={d,e,f,unused};
  backup.state={MissileGuidancePhase::Following,17,234,5,23,{71,81,2,3}};
  backup.target=0;backup.surfaceRoute={{41,25.5f},{71,40.5f}};backup.point=1;backup.start={11,10.5f};
  std::vector<uint8_t> payload,roundtrip;SaveGame read;
  check(encodeSave(native,payload,error)&&decodeSave(payload,read,error),"Native missile search and backup roundtrip");
  check(encodeSave(read,roundtrip,error)&&roundtrip==payload,"Native missile search candidate/ownership order survives exactly");
  const auto& result=*read.levels.begin()->second.world.projectiles[0].guidanceNative;
  check(result.search->candidates[0].cursor==1&&result.search->seen[2].graphNode==c.graphNode&&
        result.saved->route.ownedPoints.size()==4&&result.saved->route.ownedPoints.back().graphNode==unused.graphNode&&
        result.saved->point==1&&result.saved->state.pose48==23&&result.saved->start.y==10.5f,
        "Pending search and saved route retain cursors, timer state and all graph ownership");
  auto complete=native;auto& completeShot=complete.levels.begin()->second.world.projectiles[0];
  const auto old=*completeShot.guidanceNative->saved;
  static_cast<MissileNativeRoute&>(*completeShot.guidanceNative)=old.route;completeShot.guidanceNative->saved.reset();
  completeShot.guidance=old.state;completeShot.guidanceRoute=old.surfaceRoute;completeShot.guidancePoint=old.point;
  completeShot.guidanceStart=old.start;completeShot.guidanceTarget=old.target;
  check(encodeSave(complete,roundtrip,error)&&decodeSave(roundtrip,read,error),"Completed native route retains unused owned branch");
  auto locked=original;locked.levels.erase("LEVEL/NEXT.3DG");auto& lockedShot=locked.levels.begin()->second.world.projectiles[0];
  lockedShot.guidanceNative.emplace();lockedShot.guidance={};lockedShot.guidanceRoute.clear();lockedShot.guidancePoint=0;
  lockedShot.guidanceSearch.reset();check(encodeSave(locked,roundtrip,error)&&decodeSave(roundtrip,read,error),
        "Fresh locked missile permits empty native action without graph");
  for(int fault=0;fault<17;++fault){auto bad=native;auto& w=bad.levels.begin()->second.world;auto& p=w.projectiles[0];auto& n=*p.guidanceNative;
   if(fault==0)n.search->seen[0].graphNode=99;
   if(fault==1)n.search->seen[0].x=99;
   if(fault==2)n.search->candidates[0].cursor=3;
   if(fault==3)n.search->seen.push_back(n.search->seen[0]);
   if(fault==4)n.saved->route.ownedPoints.pop_back(),n.saved->route.ownedPoints.pop_back();
   if(fault==5)n.saved->target=999;
   if(fault==6)n.saved->point=3;
   if(fault==7)n.saved->state.pose48=49;
   if(fault==8)n.search->halfExtent.x=-1;
   if(fault==9)n.search->maximumDistance=std::numeric_limits<float>::quiet_NaN();
   if(fault==10)p.guidance.phase=MissileGuidancePhase::Idle;
   if(fault==11)n.saved->route.ownedPoints.push_back(n.search->seen[0]);
   if(fault==12)w.projectiles.push_back(p);
   if(fault==13){w.actors[0].spiderAI.emplace();w.actors[0].spiderAI->patrolRoute.points={a};}
   if(fault==14)n.saved->surfaceRoute.pop_back();
   if(fault==15)n.saved->state.scanRemainingMs=std::numeric_limits<int>::min();
   if(fault==16){p.guidanceSearch=original.levels.begin()->second.world.projectiles[0].guidanceSearch;
    check(!validMissileNativeNavigationSnapshot(w),"Live state rejects simultaneous legacy and native missile searches");}
   auto unchanged=payload;check(!encodeSave(bad,unchanged,error)&&unchanged==payload,"Invalid native missile graph/action rejected transactionally");
  }
  std::vector<uint8_t> marker;for(int64_t value:{int64_t(MissileGuidancePhase::Following),int64_t(17),int64_t(234),int64_t(5),int64_t(23)})
   for(unsigned byte=0;byte<8;++byte)marker.push_back(uint8_t(uint64_t(value)>>(8*byte)));
  auto corrupt=payload;const auto at=std::search(corrupt.begin(),corrupt.end(),marker.begin(),marker.end());
  check(at!=corrupt.end(),"Locate native backup action state");*(at+32)=49;reseal(corrupt);
  SaveGame sentinel;sentinel.currentLevel="sentinel";check(!decodeSave(corrupt,sentinel,error)&&sentinel.currentLevel=="sentinel",
        "Native backup validation rejects corrupt disk state before publication");
  // Derive the genuine V26 null-native layout by removing only V27's optional
  // field. The reader must leave migration to runtime rather than invent graph points.
  auto legacy26=locked;auto& legacyShot=legacy26.levels.begin()->second.world.projectiles[0];
  legacyShot.guidanceNative.reset();legacyShot.guidance.scanRemainingMs=34567;legacyShot.guidance.pathRemainingMs=45678;
  check(encodeSave(legacy26,roundtrip,error),"V26 migration fixture encode");
  strip31WorldFields(roundtrip,legacy26);
  size_t removedTurns=0;
  for(auto at=std::search(roundtrip.begin(),roundtrip.end(),turnBytes.begin(),turnBytes.end());at!=roundtrip.end();
      at=std::search(roundtrip.begin(),roundtrip.end(),turnBytes.begin(),turnBytes.end())){
   // V30 appends the mutable shield optional immediately after enemyTurn.
   check(*(at+turnBytes.size())==1,"Locate populated v30 shield field before v26 migration");
   strip31AbsentActorTail(roundtrip,std::distance(roundtrip.begin(),at)+turnBytes.size()+9);
   roundtrip.erase(at,at+turnBytes.size()+9);++removedTurns;
  }
  check(removedTurns==2,"Remove V28 turn fields from world and retained actor before deriving V26");
  // This legacy fixture retains one original renderer channel. Remove the
  // V29 world RNG immediately before that renderer, using its saved bytes.
  std::vector<uint8_t> rngMarker;
  for(uint64_t value:{uint64_t(legacy26.levels.begin()->second.world.nativeWorldRandom.seed),uint64_t(legacy26.levels.begin()->second.world.nativeWorldRandom.counter)})
   for(unsigned byte=0;byte<8;++byte)rngMarker.push_back(uint8_t(value>>(byte*8)));
  const auto rngField=std::search(roundtrip.begin(),roundtrip.end(),rngMarker.begin(),rngMarker.end());
  check(rngField!=roundtrip.end(),"Locate V29 world RNG extension before deriving V26");roundtrip.erase(rngField,rngField+16);
  marker.clear();for(int64_t value:{int64_t(MissileGuidancePhase::Idle),int64_t(34567),int64_t(45678),int64_t(0),int64_t(1)})
   for(unsigned byte=0;byte<8;++byte)marker.push_back(uint8_t(uint64_t(value)>>(8*byte)));
  const auto nativeField=std::search(roundtrip.begin(),roundtrip.end(),marker.begin(),marker.end());
  check(nativeField!=roundtrip.end()&&*(nativeField+105)==0,"Locate absent V27 native optional");roundtrip.erase(nativeField+105,nativeField+139);roundtrip[8]=26;
  for(size_t i=0;i<8;++i)roundtrip[12+i]=uint8_t((roundtrip.size()-28)>>(8*i));reseal(roundtrip);
  check(decodeSave(roundtrip,read,error)&&!read.levels.begin()->second.world.projectiles[0].guidanceNative&&
        read.levels.begin()->second.world.projectiles[0].guidance.scanRemainingMs==34567,
        "V26 reader preserves guidance and leaves native state unknown for runtime migration");
 }
 SceneContent catalog;ObjectClass cls;cls.name="unit";cls.id={3};catalog.classes.push_back(cls);auto hash=catalogFingerprint(catalog);check(hash==5422456814080134841ULL,"Version 3 leaves version 1 catalog fingerprint unchanged");catalog.objects.push_back(placement);check(catalogFingerprint(catalog)==hash,"Mutable placements do not change catalog identity");catalog.classes[0].name="different";check(catalogFingerprint(catalog)!=hash,"Changed definitions change catalog identity");
 auto directory=std::filesystem::temp_directory_path()/("vyruz-save-test-"+std::to_string(getpid()));std::filesystem::remove_all(directory);std::filesystem::create_directory(directory);auto path=(directory/"slot.sav").string();check(writeSaveFile(path,original,error),error.c_str());check(readSaveFile(path,decoded,error),error.c_str());check(!writeSaveFile(path,invalid,error),"Invalid replacement fails");check(readSaveFile(path,decoded,error)&&decoded.currentLevel==original.currentLevel,"Failed replacement preserves prior save");
 std::filesystem::create_directory(directory/"occupied");std::ofstream(directory/"occupied"/"marker")<<"preserve";check(!writeSaveFile((directory/"occupied").string(),original,error)&&std::filesystem::exists(directory/"occupied"/"marker"),"Rename failure preserves destination");for(auto& entry:std::filesystem::directory_iterator(directory))check(entry.path().string().find(".tmp.")==std::string::npos,"Failed write removes temporary file");std::filesystem::remove_all(directory);
}
