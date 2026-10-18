#include "game/save_file.h"
#include "game/lift_access.h"
#include <bit>
#include <zlib.h>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <cerrno>
#include <fcntl.h>
#include "platform/files.h"
namespace vyruz { namespace {
constexpr size_t MaxFile=256*1024*1024,MaxCount=100000,MaxString=1024*1024;
struct Failure:std::runtime_error {using std::runtime_error::runtime_error;};
template<class A,class T> void fields(A&,T&);
template<class T> struct Optional:std::false_type{};
template<class T> struct Optional<std::optional<T>>:std::true_type{};
template<class T> struct Vector:std::false_type{};
template<class T> struct Vector<std::vector<T>>:std::true_type{};
template<class T> struct Map:std::false_type{};
template<class K,class V> struct Map<std::map<K,V>>:std::true_type{};
template<class T> struct Set:std::false_type{};
template<class T> struct Set<std::set<T>>:std::true_type{};
struct Archive {
 uint32_t version=35;
 bool reading; std::vector<uint8_t> bytes; std::span<const uint8_t> input; size_t pos=0,depth=0,allocationRemaining=1024*1024*1024;
 Archive():reading(false){} explicit Archive(std::span<const uint8_t> in):reading(true),input(in){}
 uint64_t integer(uint64_t n,size_t size){
  if(reading){if(size>input.size()-pos)throw Failure("Truncated save");n=0;for(size_t i=0;i<size;++i)n|=uint64_t(input[pos++])<<(i*8);}
  else {if(bytes.size()+size>MaxFile)throw Failure("Save exceeds 256 MiB limit");for(size_t i=0;i<size;++i)bytes.push_back(uint8_t(n>>(i*8)));}return n;
 }
 void allocate(size_t count,size_t width){if(!reading)return;if(count>allocationRemaining/width)throw Failure("Save allocation limit exceeded");allocationRemaining-=count*width;}
 template<class... T>void operator()(T&... v){(value(v),...);}
 template<class T> void value(T& v){
  if(++depth>128)throw Failure("Save nesting limit exceeded");
  if constexpr(std::is_same_v<T,bool>){auto n=integer(v?1:0,1);if(n>1)throw Failure("Invalid save boolean");v=n;}
  else if constexpr(std::is_integral_v<T>){
   // Fixed-width scalars keep version 1 independent of size_t/int ABI widths.
   uint64_t encoded;if constexpr(std::is_signed_v<T>)encoded=uint64_t(int64_t(v));else encoded=uint64_t(v);
   auto n=integer(encoded,8);
   if constexpr(std::is_signed_v<T>){auto signedValue=std::bit_cast<int64_t>(n);if(signedValue<std::numeric_limits<T>::min()||signedValue>std::numeric_limits<T>::max())throw Failure("Saved integer out of range");v=static_cast<T>(signedValue);}
   else {if(n>std::numeric_limits<T>::max())throw Failure("Saved integer out of range");v=static_cast<T>(n);}
  }
  else if constexpr(std::is_floating_point_v<T>){using U=std::conditional_t<sizeof(T)==4,uint32_t,uint64_t>;U n=std::bit_cast<U>(v);value(n);v=std::bit_cast<T>(n);if(!std::isfinite(v))throw Failure("Non-finite save number");}
  else if constexpr(std::is_enum_v<T>){uint32_t n=uint32_t(v);value(n);uint32_t limit=0;if constexpr(std::is_same_v<T,TextKind>)limit=2;else if constexpr(std::is_same_v<T,BrainBase>)limit=4;else if constexpr(std::is_same_v<T,RouteStatus>)limit=4;else if constexpr(std::is_same_v<T,MissileGuidancePhase>)limit=2;else static_assert(!sizeof(T),"Unrecognized enum");if(n>limit)throw Failure("Invalid save enum");v=T(n);}
  else if constexpr(std::is_same_v<T,std::string>){uint64_t n=v.size();value(n);if(n>MaxString)throw Failure("Save string limit exceeded");if(reading){allocate(size_t(n),1);if(n>input.size()-pos)throw Failure("Truncated save string");v.assign(reinterpret_cast<const char*>(input.data()+pos),size_t(n));pos+=size_t(n);}else{if(n>MaxFile-bytes.size())throw Failure("Save exceeds size limit");bytes.insert(bytes.end(),v.begin(),v.end());}}
  else if constexpr(Optional<T>::value){bool present=v.has_value();value(present);if(reading){if(present)v.emplace();else v.reset();}if(present)value(*v);}
  else if constexpr(Vector<T>::value){uint64_t n=v.size();value(n);if(n>MaxCount||(reading&&n>input.size()-pos))throw Failure("Save collection limit exceeded");if(reading){allocate(size_t(n),sizeof(typename T::value_type));v.resize(size_t(n));}for(auto& x:v)value(x);}
  else if constexpr(Map<T>::value){uint64_t n=v.size();value(n);if(n>MaxCount||(reading&&n>input.size()-pos))throw Failure("Save map limit exceeded");if(reading){allocate(size_t(n),sizeof(typename T::value_type)+48);v.clear();for(uint64_t i=0;i<n;++i){typename T::key_type k{};typename T::mapped_type x{};(*this)(k,x);if(!v.emplace(std::move(k),std::move(x)).second)throw Failure("Duplicate save map key");}}else for(auto& [key,x]:v){auto k=key;(*this)(k,x);}}
  else if constexpr(Set<T>::value){uint64_t n=v.size();value(n);if(n>MaxCount||(reading&&n>input.size()-pos))throw Failure("Save set limit exceeded");if(reading){allocate(size_t(n),sizeof(typename T::value_type)+48);v.clear();for(uint64_t i=0;i<n;++i){typename T::value_type x{};value(x);if(!v.insert(x).second)throw Failure("Duplicate save set entry");}}else for(auto x:v)value(x);}
  else fields(*this,v);
  --depth;
 }
};
#define TYPE(T,...) if constexpr(std::is_same_v<V,T>) a(__VA_ARGS__); else
// Explicit members keep padding, addresses and library representations out of the file.
template<class A,class V>void fields(A& a,V& v){
 TYPE(ClassId,v.value) TYPE(ObjectId,v.value) TYPE(SequenceId,v.value) TYPE(PathId,v.value)
 TYPE(ContentPosition,v.x,v.y) TYPE(SpriteReference,v.path,v.sequence)
 TYPE(ActorFlags,v.invisible,v.detail,v.inGround,v.overWater,v.zShadow,v.managed,v.inertia)
 TYPE(TextNode,v.kind,v.key,v.arguments,v.values,v.children,v.source,v.line)
 TYPE(PatrolReference,v.path,v.point,v.resolved)
 TYPE(ObjectPlacement,v.id,v.classId,v.owner,v.position,v.flags,v.sprite,v.shadow,v.behaviours,v.properties,v.patrols,v.authored)
 TYPE(ObjectClass,v.id,v.name,v.parent,v.properties,v.sprite,v.shadow,v.flags,v.behaviours)
 TYPE(PatrolPath,v.id,v.points,v.authored)
 TYPE(VitalStats,v.energy,v.energyMin,v.energyMax,v.shield,v.shieldMin,v.shieldMax,v.money,v.lifetimeMoney)
 TYPE(PickupEffect,v.energy,v.shield,v.money)
 TYPE(HeldCode,v.object,v.mapPath,v.className,v.remainingMs,v.classTimeoutMs)
 if constexpr(std::is_same_v<V,KeyPiece>){
  a(v.id,v.code,v.part,v.total,v.text);
  if(a.version>=9)a(v.held);else if(a.reading)v.held.reset();
 }else
 TYPE(WeaponConfig,v.delayMs,v.weaponsGap,v.leftSide,v.rightSide,v.alternateSides)
 if constexpr(std::is_same_v<V,WeaponState>){
  a(v.lastFireMs,v.alternateLeft);
  if(a.version>=18)a(v.slopeRadians,v.slopeHeld);else if(a.reading){v.slopeRadians=0;v.slopeHeld=false;}
 }else
 if constexpr(std::is_same_v<V,EquippedWeapon>){
  a(v.name,v.ammo,v.config,v.state,v.range,v.visibleGunFire);
  if(a.version>=8)a(v.ammoSlot);else if(a.reading)v.ammoSlot.reset();
 }else
 if constexpr(std::is_same_v<V,OrderedInventory<AmmoStock>>){
  if(a.version>=8){
   uint64_t n=v.size();a(n);if(n>MaxCount || (a.reading && n>a.input.size()-a.pos))throw Failure("Save ammo limit exceeded");
   if(a.reading){a.allocate(size_t(n),sizeof(OrderedInventory<AmmoStock>::value_type));v.slots.resize(size_t(n));}
   for(auto& [name,stock]:v)a(name,stock);
  }else {
   std::map<std::string,AmmoStock> legacy;a(legacy);
   if(a.reading){v.clear();for(const auto& [name,stock]:legacy)v.append(name,stock);}
  }
 }else
 TYPE(ActorCreate,v.classId,v.offset,v.nextMs,v.repeatMs,v.trailStep,v.previousOrigin,v.direction,v.done,v.currentPosition)
 TYPE(GeneratorState,v.product,v.maxBirths,v.birthCount,v.delayMs,v.durationMs,v.remainingMs,v.stressable,v.active,v.stablePopulation)
 TYPE(AmmoStock,v.count,v.maximum)
 TYPE(FxTrailSegment,v.previous,v.step)
 TYPE(PendingExplosion,v.damage,v.radius,v.remainingMs,v.altitude,v.creator,v.animation,v.initialized)
 TYPE(TimedProtectionState,v.onRemainingMs,v.offRemainingMs,v.active)
 TYPE(BrainConfig,v.base,v.seeker,v.tracker,v.guardian,v.guardianCenterFollowsActor,v.targetClass,v.sensorRange,v.minimumDistance,v.scanDelayMs,v.visionDegrees,v.guardianRange,v.home,v.center,v.pathId,v.reverse,v.closed,v.randomRange,v.randomDelayMs)
 TYPE(NativeNavObstacle,v.minX,v.minY,v.maxX,v.maxY)
 TYPE(NativeNavGraphObject,v.reference,v.bounds,v.alive,v.point)
 TYPE(NativeNavGraphEdge,v.object,v.maximum)
 TYPE(NativeNavGraph,v.objects,v.xEdges,v.yEdges)
 TYPE(NativeNavPoint,v.x,v.y,v.graphNode)
 TYPE(NativeNavCandidate,v.points,v.cursor)
 TYPE(NativeNavState,v.candidates,v.seen,v.halfExtent,v.maximumDistance)
 TYPE(SpiderActionState,v.searching,v.active,v.scanRemainingMs,v.searchRemainingMs)
 TYPE(SpiderPatrolState,v.initialized,v.reverse,v.point)
 TYPE(SpiderRouteCore,v.action,v.search,v.points,v.cursor,v.segmentStart,v.segmentEnd,v.sampledTarget,v.target,v.status)
 TYPE(SpiderRouteState,static_cast<SpiderRouteCore&>(v),v.saved)
 TYPE(SpiderMotionHistory,v.center,v.previousSurface)
 if constexpr(std::is_same_v<V,EnemyTurnState>){
  a(v.bodyPose,v.bodyDesiredPose,v.bodyPreviousPose,v.gunPose,v.gunDesiredPose,v.gunPreviousPose,v.bodyPeriodMs,v.gunPeriodMs,v.bodyRemainingMs,v.gunRemainingMs,v.logicalHeading,v.orthogonalHeading,v.turnCommand,v.lean,v.fastTurn);
  if(a.version>=30)a(v.gunLogicalHeading,v.gunCommand,v.gunPreviousCommand,v.gunInRange,v.gunStateKnown);
  else if(a.reading){v.gunLogicalHeading=v.gunCommand=v.gunPreviousCommand=0;v.gunInRange=false;v.gunStateKnown=false;}
  // Content-specific 24/48-pose rings are checked by World restore. The
  // portable file contract accepts their common domain, never the deliberately
  // out-of-ring 0/N+1 states used to stress the original arithmetic helper.
  for(int pose:{v.bodyPose,v.bodyDesiredPose,v.bodyPreviousPose,v.gunPose,v.gunDesiredPose,v.gunPreviousPose})
   if(pose<1||pose>48)throw Failure("Invalid enemy turn pose");
  for(int heading:{v.logicalHeading,v.orthogonalHeading,v.gunLogicalHeading})
   if(heading<0||heading>23)throw Failure("Invalid enemy turn heading");
  if(v.turnCommand!=0&&v.turnCommand!=203&&v.turnCommand!=205)throw Failure("Invalid enemy turn command");
  for(int command:{v.gunCommand,v.gunPreviousCommand})
   if(command!=0&&command!=203&&command!=205)throw Failure("Invalid enemy gun command");
  if(v.lean< -15||v.lean>15)throw Failure("Invalid enemy turn lean");
  for(int period:{v.bodyPeriodMs,v.gunPeriodMs})
   if(period< -100000000||period>100000000)throw Failure("Invalid enemy turn period");
  if(v.bodyRemainingMs<0||v.bodyRemainingMs>100000000 ||
     v.gunRemainingMs< -100000000||v.gunRemainingMs>100000000)throw Failure("Invalid enemy turn timer");
 }else
 TYPE(NativeRandomState,v.seed,v.counter)
 TYPE(NativeActorPoolState,v.nextGeneration,v.liveCount,v.catalog,v.generations)
 TYPE(NativePoolTileBody,v.x,v.y,v.source,v.layer,v.generation,v.poolSlot,v.arrivalOrder)
 TYPE(NativeWorldPoolState,v.pool,v.tiles,v.originalHistoryKnown)
 TYPE(NativeAwakeManager,v.entries)
 TYPE(NativeAwakeState,v.manager,v.actors,v.ammo)
 TYPE(NativePlayerFacingState,v.pose,v.desiredPose,v.previousPose,v.logicalHeading,v.orthogonalHeading,v.remainingMs,v.periodMs,v.fastTurn)
 if constexpr(std::is_same_v<V,NativeActorCells>){
  a(v.width,v.height,v.heads,v.next,v.cell);
  if(a.version>=32)a(v.arrivalOrder,v.nextArrivalOrder);
  else if(a.reading){
   // Pre32 retains each cell's exact linked order, but has no cross-body
   //insertion clock. Assign increasing ordinals from each tail to its head.
   v.arrivalOrder.assign(v.cell.size(),0);v.nextArrivalOrder=1;
   std::vector<bool> seen(v.cell.size(),false);
   for(auto head:v.heads){std::vector<size_t> chain;
    for(auto id=head;id!=NativeActorCells::Missing;id=v.next[id]){
     if(id>=v.cell.size()||id>=v.next.size()||seen[id])throw Failure("Invalid legacy actor cell list");
     seen[id]=true;chain.push_back(id);
    }
    for(auto it=chain.rbegin();it!=chain.rend();++it)v.arrivalOrder[*it]=v.nextArrivalOrder++;
   }
  }
 }else
 TYPE(NativeAmmoOwnerCounters,v.misses,v.hits,v.kills,v.activeAmmo)
 if constexpr(std::is_same_v<V,NativeBareAmmoBody>){
  a(v.energy,v.shield,v.damagePending,v.killCounted,v.motion);
  if(a.version>=34)a(v.nativeHit);else if(a.reading)v.nativeHit=false;
 }else
 TYPE(NativeAmmoCellLink,v.next,v.arrivalOrder,v.cell)
 if constexpr(std::is_same_v<V,NativeAmmoCells>){
  a(v.width,v.height,v.heads);
  // Runtime lookup is hashed; disk order remains deterministic.
  std::map<uint64_t,NativeAmmoCellLink> ordered;
  if(!a.reading)for(const auto& entry:v.links)ordered.insert(entry);
  a(ordered);
  if(a.reading){v.links.clear();for(const auto& entry:ordered)v.links.insert(entry);}
  if(!v.valid())throw Failure("Invalid bare Ammo cells");
 }else
 TYPE(NativeEnemyActionClock,v.searching,v.active,v.scanRemainingMs,v.pathRemainingMs)
 TYPE(NativeEnemyRouteCore,v.clock,v.search,v.points,v.ownedPoints,v.cursor,v.segmentStart,v.segmentEnd,v.sampledTarget,v.sampledCellX,v.sampledCellY,v.target,v.desiredHeading,v.status)
 if constexpr(std::is_same_v<V,NativeEnemyRouteState>){a(static_cast<NativeEnemyRouteCore&>(v),v.saved);}else
 TYPE(NativeEnemyPatrolState,v.initialized,v.reverse,v.point,v.goal,v.goalCellX,v.goalCellY)
 TYPE(NativeEnemyRandomState,v.reuseCount,v.replayRandom)
 TYPE(OrdinaryEnemyNativeState,v.targeting,v.base,v.patrol,v.random,v.actorFlags,v.lastAction)
 if constexpr(std::is_same_v<V,SpiderAIState>){a(v.patrol,v.patrolRoute,v.seekerRoute,v.actorFlags);if(a.version>=25)a(v.motion);else if(a.reading)v.motion.reset();if(a.version>=29)a(v.turn);else if(a.reading)v.turn.reset();}else
 TYPE(BrainState,v.scanRemainingMs,v.acquired,v.initialized,v.reverse,v.point)
 TYPE(GateConfig,v.flags,v.delayMs,v.keyId,v.baseSequence)
 TYPE(GateState,v.flags,v.remainingMs,v.keyId,v.sequence)
 TYPE(LiftConfig,v.flags,v.delayMs,v.keyId,v.baseSequence)
 TYPE(LiftState,v.flags,v.remainingMs,v.keyId,v.sequence,v.passable)
 TYPE(NavigationRoute,v.status,v.points,v.expanded)
 if constexpr(std::is_same_v<V,NativeGeneratorSquareState>){
  a(v.centerX,v.centerZ,v.minX,v.minZ,v.maxX,v.maxZ);
  if(!validNativeGeneratorSquare(v))throw Failure("Invalid native square bounds");
 }else
 TYPE(NavigationSearchState::Node,v.index,v.cost,v.parent,v.closed)
 TYPE(NavigationSearchState::Entry,v.estimate,v.cost,v.index)
 TYPE(NavigationSearchState,v.start,v.goal,v.radius,v.width,v.height,v.limit,v.initialized,v.result,v.nodes,v.open)
 if constexpr(std::is_same_v<V,NavigationSearch>){NavigationSearchState s;if(!a.reading)s=v.capture();a(s);if(a.reading){Level level;if(s.width<=0||s.height<=0||s.width%2||s.height%2)throw Failure("Invalid navigation dimensions");level.mapW=s.width/2;level.mapH=s.height/2;if(!v.restore(level,s))throw Failure("Invalid saved navigation search");}}else
 // visualThrust/engineGlowMs/hitFlashMs/headlightVisible/locomoting are transient presentation.
 if constexpr(std::is_same_v<V,ActorState>){
  bool cancelRetreat=false;
  a(v.alive,v.pickup,v.solid,v.indestructible,v.enemy,v.directional,v.reached,v.effect,v.hudEnemy,v.direction,v.fireRevision,v.radius,v.speed,v.turnMs,v.lifeMs,v.vitals,v.pickupEffect,v.brain,v.brainState,v.weapons,v.ammo,v.selectedWeapon,v.keys,v.heldKeys,v.ageMs,v.pickupDelayMs,v.route,v.routePoint,v.routeSearch,v.routeGoal,v.randomGoal,v.routeRetryMs,v.randomWaitMs,v.gate,v.gateState,v.gateRevision,v.gateAnimationFinished);
  if(a.version>=9)a(v.itemTimeoutMs);else if(a.reading)v.itemTimeoutMs.reset();
  if(a.version>=5)a(v.reactivateMs);else if(a.reading)v.reactivateMs=-1;
  if(a.version>=6)a(v.explosion,v.projectileAltitude);else if(a.reading){v.explosion.reset();v.projectileAltitude.reset();}
  if(a.version>=7)a(v.trailSegment);else if(a.reading)v.trailSegment.reset();
  if(a.version>=11)a(v.pooledTrail,v.visualRevision);
  else if(a.reading){v.pooledTrail=false;v.visualRevision=0;}
  if(a.version>=10)a(v.generators,v.generatorParent,v.generatorsInitialized);
  else if(a.reading){v.generators.clear();v.generatorParent.reset();v.generatorsInitialized=false;}
  if(a.version>=13)a(v.creates,v.createsInitialized);
  else if(a.reading){v.creates.clear();v.createsInitialized=false;}
  if(a.version>=14){a(v.lift);if(v.lift)a(v.liftState);}
  else if(a.reading){v.lift.reset();v.liftState={};}
  if(a.version>=15)a(v.timedProtection);else if(a.reading)v.timedProtection={};
  if(a.version>=26)a(v.nativeMotion);else if(a.reading)v.nativeMotion.reset();
  if(a.version>=35)a(v.nativeGeneratorSquare);else if(a.reading)v.nativeGeneratorSquare.reset();
  if(a.version>=24)a(v.spiderAI);else if(a.reading)v.spiderAI.reset();
  if(a.version>=23)a(v.spiderPatrolTurning);
  else if(a.reading)v.spiderPatrolTurning=false;
  if(a.version==22){float retreat=0,cooldown=0,x=0,z=0;a(retreat,cooldown,x,z);cancelRetreat=retreat>0;}
  if(a.version>=21)a(v.enemyMoveCommand,v.enemyMovedLastStep);
  else if(a.reading){v.enemyMoveCommand=0;v.enemyMovedLastStep=false;}
  if(a.version>=20)a(v.enemyThrust,v.enemyMagnitude,v.enemyMotionInitialized,v.driveSegmentStart,v.driveSegmentEnd);
  else if(a.reading){v.enemyThrust=v.enemyMagnitude=0;v.enemyMotionInitialized=false;v.driveSegmentStart.reset();v.driveSegmentEnd.reset();}
  if(a.version>=17)a(v.contactVelocity.x,v.contactVelocity.z,v.contactMotion);
  else if(a.reading){v.contactVelocity={};v.contactMotion=false;}
  if(cancelRetreat){v.contactVelocity={};v.enemyThrust=v.enemyMagnitude=0;v.contactMotion=false;v.enemyMoveCommand=0;}
  if(a.version>=28)a(v.enemyTurn);else if(a.reading)v.enemyTurn.reset();
  if(a.version>=30)a(v.shieldUpStep);else if(a.reading)v.shieldUpStep.reset();
  if(a.version>=31)a(v.nativeDamagePending,v.nativeKillCounted);
  else if(a.reading){v.nativeDamagePending=false;v.nativeKillCounted=false;}
  if(a.version>=31)a(v.ordinaryAI);else if(a.reading)v.ordinaryAI.reset();
  if(a.version>=34)a(v.nativeHit,v.nativePoolSlot,v.nativeFireRequested);else if(a.reading){v.nativeHit=false;v.nativePoolSlot=SIZE_MAX;v.nativeFireRequested=false;}
  if(a.version>=32)a(v.nativeGeneration,v.nativeCreatorGeneration,v.nativeAmmoOwner);
  else if(a.reading){v.nativeGeneration=v.nativeCreatorGeneration=0;v.nativeAmmoOwner={};}
 }else
 TYPE(HeldItem,v.object,v.actor,v.mapPath,v.className,v.holder,v.surprise)
 TYPE(MissileTargetSample,v.x,v.y,v.tileX,v.tileY)
 TYPE(MissileGuidanceState,v.phase,v.scanRemainingMs,v.pathRemainingMs,v.turnRemainingMs,v.pose48,v.sampledTarget)
 TYPE(MissileNativeRoute,v.search,v.points,v.ownedPoints)
 TYPE(MissileSavedRoute,v.route,v.state,v.target,v.surfaceRoute,v.point,v.start)
 TYPE(MissileNativeNavigation,static_cast<MissileNativeRoute&>(v),v.saved)
 TYPE(ProjectileMotionConfig,v.speedMax,v.acceleration,v.deceleration,v.weight,v.grip,v.frictionX,v.frictionZ,v.thresholdX,v.thresholdZ,v.inertia,v.launched)
 TYPE(ProjectileMotionState,v.velocityX,v.velocityZ,v.magnitude,v.thrust)
 if constexpr(std::is_same_v<V,ProjectileCreate>){
  a(v.classId,v.offset,v.nextMs,v.repeatMs,v.done,v.currentPosition);
  if(a.version>=7)a(v.trailStep,v.previousOrigin);
 }else
 if constexpr(std::is_same_v<V,Projectile>){
  if(a.version>=24)a(v.spiderGraphNode);else if(a.reading)v.spiderGraphNode=SIZE_MAX;
  a(v.position,v.velocity,v.motionConfig,v.motion,v.directionX,v.directionZ,v.owner,v.visual,v.classId,v.radius,v.damage,v.damageRadius,v.distance,v.range,v.ageMs,v.lifetimeMs,v.activeAfterMs,v.fuseMs,v.ignoreOwner,v.alive,v.hitEffect);
  if(a.version>=6)a(v.creates,v.inactiveMs,v.onMs,v.damageRemainingMs,v.phaseInitialized,v.phaseHidden);
  if(a.version>=18)a(v.slopeEnabled,v.slopeRadians,v.slopeHeight,v.slopeAltitude);
  else if(a.reading){v.slopeEnabled=false;v.slopeRadians=v.slopeHeight=v.slopeAltitude=0;}
  if(a.version>=19){
   a(v.guidanceClass,v.guidanceTarget,v.guidance,v.guidanceRoute,v.guidancePoint,v.guidanceStart,v.guidanceSearch);
   if(v.guidanceSearch){
    const auto state=v.guidanceSearch->capture();Level level;NavigationSearch checked;
    if(state.width<=0 || state.height<=0 || state.width%2 || state.height%2)throw Failure("Invalid missile search dimensions");
    level.mapW=state.width/2;level.mapH=state.height/2;
    if(!checked.restore(level,state))throw Failure("Invalid missile search state");
   }
   for(int timer:{v.guidance.scanRemainingMs,v.guidance.pathRemainingMs,v.guidance.turnRemainingMs})
    if(timer< -100000000 || timer>100000000)throw Failure("Invalid missile guidance timer");
   if(v.guidance.pose48<1 || v.guidance.pose48>48 || v.guidancePoint>v.guidanceRoute.size())throw Failure("Invalid missile guidance pose or route cursor");
  }else if(a.reading){v.guidanceClass.reset();v.guidanceTarget.reset();v.guidance={};v.guidanceRoute.clear();v.guidancePoint=0;v.guidanceStart={};v.guidanceSearch.reset();}
  if(a.version>=27)a(v.guidanceNative);else if(a.reading)v.guidanceNative.reset();
  if(a.version>=34)a(v.nativePoolSlot);else if(a.reading)v.nativePoolSlot=SIZE_MAX;
  if(a.version>=32)a(v.nativeGeneration,v.nativeOwnerGeneration,v.nativeBodyIdentity,v.nativeBareBody);
  else if(a.reading){v.nativeGeneration=v.nativeOwnerGeneration=0;v.nativeBodyIdentity=0;v.nativeBareBody.reset();}

 }else
 if constexpr(std::is_same_v<V,PlayerMotion>){
  if(a.version==1){
   // Version 1 stored projected pixels/second. Native physics stores X/Z per 14 ms.
   float screenX=0,screenY=0;a(v.direction,v.turnSeconds,screenX,screenY);
   v.velocityX=(.559016994f*screenX+1.118033989f*screenY)*.014f;
   v.velocityZ=(-.559016994f*screenX+1.118033989f*screenY)*.014f;
   v.magnitude=std::hypot(v.velocityX,v.velocityZ);v.thrust=0;
   if(!std::isfinite(v.magnitude))throw Failure("Invalid migrated player motion");
  }else a(v.direction,v.turnSeconds,v.velocityX,v.velocityZ,v.magnitude,v.thrust);
 }else
 TYPE(TransitionRequest,v.lift,v.path,v.cost)
 if constexpr(std::is_same_v<V,WorldSnapshot>){
  if(a.version>=31)a(v.actorCells);else if(a.reading)v.actorCells.reset();
  if(a.version>=24)a(v.spiderNavigation);else if(a.reading)v.spiderNavigation.reset();
  a(v.catalogHash,v.objects,v.actors,v.projectiles,v.player,v.navigationCursor,v.motion,v.elapsedMs,v.playerKills,v.shotsFired,v.pickupsTaken,v.actorsDestroyed,v.transition,v.rejectedLifts,v.randomState,v.message,v.savePlot,v.saveDwellMs,v.saveAttempted);
  if(a.version>=3)a(v.difficulty);else if(a.reading)v.difficulty=1;
  if(a.version>=12)a(v.heldItems);else if(a.reading)v.heldItems.clear();
  if(a.reading)v.heldItemsKnown=a.version>=12;
  if(a.version>=15)a(v.endingSource,v.endingRemainingMs);
  else if(a.reading){v.endingSource.reset();v.endingRemainingMs=0;}
  if(a.reading)v.timedProtectionKnown=a.version>=15;
  if(a.version>=16)a(v.ammoHits,v.ammoMisses);else if(a.reading){v.ammoHits=0;v.ammoMisses=0;}
  if(a.version>=29)a(v.nativeWorldRandom);else if(a.reading)v.nativeWorldRandom={};
  if(a.version>=31)a(v.nativeGlobalRandom);else if(a.reading)v.nativeGlobalRandom={};
  if(a.version>=32)a(v.nativeAmmoLifecycleKnown,v.bareAmmoCells,v.nextNativeGeneration,v.nextNativeCellOrder,v.nextNativeBodyIdentity);
  else if(a.reading){v.nativeAmmoLifecycleKnown=false;v.bareAmmoCells={};v.nextNativeGeneration=1;v.nextNativeCellOrder=v.nextNativeBodyIdentity=1;}
  //v33 appends only pending movement identities; prior actor layouts stay intact.
  std::vector<size_t> continuations;
  if(!a.reading)for(size_t id=0;id<v.actors.size();++id)
   if(v.actors[id].nativeAmmoContinuation)continuations.push_back(id);
  if(a.version>=33)a(continuations);
  if(a.reading){
   for(auto& actor:v.actors)actor.nativeAmmoContinuation=false;
   std::set<size_t> seen;
   for(auto id:continuations){
    if(id>=v.actors.size() || !seen.insert(id).second)throw Failure("Invalid pending Ammo movement identity");
    v.actors[id].nativeAmmoContinuation=true;
   }
  }
  if(a.version>=34)a(v.nativeAwake,v.nativePlayerFacing,v.nativePlayerPreviousSurface,v.nativeReplayRandom,v.nativePool);
  else if(a.reading){v.nativePool.reset();v.nativeAwake.reset();v.nativePlayerFacing.reset();v.nativePlayerPreviousSurface={};v.nativeReplayRandom={};}
  if(v.difficulty<0 || v.difficulty>3)throw Failure("Invalid saved difficulty");
 }else
 TYPE(AnimationCursorState,v.cursor,v.flags,v.remaining)
 if constexpr(std::is_same_v<V,SceneChannelState>){
  a(v.object,v.shadow,v.bankPath,v.sequence,v.cursor,v.gateRevision,v.firing,v.fireRevision,v.lift);
  if(a.version>=11)a(v.visualRevision);else if(a.reading)v.visualRevision=0;
 }else
 // Native disk saves recreate transient No-saving FX. The optional live
 // effects snapshot is deliberately omitted; v26 channel identities stay valid.
 if constexpr(std::is_same_v<V,SceneRendererState>){
  a(v.objectCount,v.channels);if(a.reading)v.effects.reset();
 }else
 TYPE(SavedLevel,v.world,v.render)
 if constexpr(std::is_same_v<V,SaveGame>){
  a(v.currentLevel,v.levels);
  if(a.version>=4)a(v.restartLevel);else if(a.reading)v.restartLevel.reset();
 }else
 static_assert(!sizeof(V),"Missing save member encoding");
}
#undef TYPE
uint64_t hash(std::span<const uint8_t> b){uint64_t h=14695981039346656037ULL;for(auto x:b){h^=x;h*=1099511628211ULL;}return h;}
void validate(const SaveGame& s){
 if(!validSaveLevelPath(s.currentLevel)||!s.levels.contains(s.currentLevel)||s.levels.size()>256)throw Failure("Invalid saved current level");
 for(auto& [path,l]:s.levels){if(!validSaveLevelPath(path))throw Failure("Invalid saved level path");if(l.world.transition&&!endingDestination(l.world.transition->path)&&!validSaveLevelPath(l.world.transition->path))throw Failure("Invalid saved transition path");}
 auto guidanceReferences=[](const WorldSnapshot& world){
  if(!validSpiderNavigationSnapshot(world))throw Failure("Invalid Spider navigation state");
  if(!validMissileNativeNavigationSnapshot(world))throw Failure("Invalid native missile navigation state");
  for(const auto& shot:world.projectiles)
   if(shot.guidanceTarget && *shot.guidanceTarget>=world.actors.size())throw Failure("Invalid missile target reference");
 };
 for(const auto& [path,level]:s.levels)guidanceReferences(level.world);
 if(s.restartLevel)guidanceReferences(s.restartLevel->world);
 if(s.restartLevel){
  const auto& retry=s.restartLevel->world;
  if(retry.catalogHash!=s.levels.at(s.currentLevel).world.catalogHash ||
     retry.player>=retry.actors.size() || !retry.actors[retry.player].alive || retry.transition)
   throw Failure("Invalid restart checkpoint");
 }
}
constexpr char Magic[]="VYRZSAVE";
} // namespace
bool validSaveLevelPath(const std::string& p){
 if(p.empty()||p.size()>1024||p.front()=='/'||p.find('\\')!=std::string::npos||p.find(':')!=std::string::npos||p.find('\0')!=std::string::npos||p.find("//")!=std::string::npos)return false;
 std::filesystem::path path(p);for(const auto& part:path)if(part==".."||part==".")return false;
 auto ext=path.extension().string();for(char& c:ext)if(c>='a'&&c<='z')c-=32;return ext==".3DG";
}
bool encodeSave(SaveGame&& s,std::vector<uint8_t>& out,std::string& error){try{validate(s);for(auto& [path,level]:s.levels)normalizeSavedSurprise(level.world);if(s.restartLevel)normalizeSavedSurprise(s.restartLevel->world);Archive payload;payload(s);Archive a;for(size_t i=0;i<8;++i)a.integer(uint8_t(Magic[i]),1);a.integer(payload.version,4);a.integer(payload.bytes.size(),8);a.integer(hash(payload.bytes),8);if(payload.bytes.size()>MaxFile-a.bytes.size())throw Failure("Save exceeds 256 MiB limit");a.bytes.insert(a.bytes.end(),payload.bytes.begin(),payload.bytes.end());out=std::move(a.bytes);error.clear();return true;}catch(const std::exception& e){error=e.what();return false;}}
bool encodeSave(const SaveGame& source,std::vector<uint8_t>& out,std::string& error){try{SaveGame owned=source;return encodeSave(std::move(owned),out,error);}catch(const std::exception& e){error=e.what();return false;}}
// Storage envelope1 contains the unchanged versioned VYRZSAVE stream.
// Keep raw encoding available for deterministic comparisons and legacy fixtures.
bool packSave(std::vector<uint8_t>& bytes,std::string& error){try{
 uLongf size=compressBound(static_cast<uLong>(bytes.size()));
 std::vector<uint8_t> packed(size);
 if(compress2(packed.data(),&size,bytes.data(),static_cast<uLong>(bytes.size()),Z_BEST_SPEED)!=Z_OK)
  throw Failure("Save compression failed");
 if(size+20<bytes.size()){
  Archive header;for(char c:std::string_view("VYRZPACK",8))header.integer(uint8_t(c),1);
  header.integer(1,4);header.integer(bytes.size(),8);
  header.bytes.insert(header.bytes.end(),packed.begin(),packed.begin()+size);
  bytes=std::move(header.bytes);
 }
 error.clear();return true;
}catch(const std::exception& e){error=e.what();return false;}}
bool encodeStoredSave(SaveGame&& save,std::vector<uint8_t>& out,std::string& error){
 std::vector<uint8_t> bytes;if(!encodeSave(std::move(save),bytes,error)||!packSave(bytes,error))return false;
 out=std::move(bytes);return true;
}
bool encodeStoredSave(const SaveGame& save,std::vector<uint8_t>& out,std::string& error){
 std::vector<uint8_t> bytes;if(!encodeSave(save,bytes,error)||!packSave(bytes,error))return false;
 out=std::move(bytes);return true;
}
bool decodeSave(std::span<const uint8_t> bytes,SaveGame& out,std::string& error){try{if(bytes.size()>MaxFile)throw Failure("Save exceeds 256 MiB limit");std::vector<uint8_t> inflated;
 if(bytes.size()>=8 && std::memcmp(bytes.data(),"VYRZPACK",8)==0){
  Archive envelope(bytes);envelope.pos=8;
  if(envelope.integer(0,4)!=1)throw Failure("Unsupported save compression version");
  const auto size=envelope.integer(0,8);
  if(size<28 || size>MaxFile)throw Failure("Invalid uncompressed save size");
  inflated.resize(static_cast<size_t>(size));
  z_stream stream{};stream.next_in=const_cast<Bytef*>(bytes.data()+envelope.pos);
  stream.avail_in=static_cast<uInt>(bytes.size()-envelope.pos);
  stream.next_out=inflated.data();stream.avail_out=static_cast<uInt>(inflated.size());
  if(inflateInit(&stream)!=Z_OK)throw Failure("Cannot initialize save decompression");
  const int result=inflate(&stream,Z_FINISH);
  const bool complete=result==Z_STREAM_END && stream.total_out==size && stream.avail_in==0;
  inflateEnd(&stream);
  if(!complete)throw Failure("Invalid compressed save data");
  bytes=inflated; // The normal magic/checksum/validation below also rejects nested envelopes.
 }
 Archive header(bytes);for(size_t i=0;i<8;++i)if(header.integer(0,1)!=uint8_t(Magic[i]))throw Failure("Not a Vyruz port save");auto version=header.integer(0,4);if(version!=1&&version!=2&&version!=3&&version!=4&&version!=5&&version!=6&&version!=7&&version!=8&&version!=9&&version!=10&&version!=11&&version!=12&&version!=13&&version!=14&&version!=15&&version!=16&&version!=17&&version!=18&&version!=19&&version!=20&&version!=21&&version!=22&&version!=23&&version!=24&&version!=25&&version!=26&&version!=27&&version!=28&&version!=29&&version!=30&&version!=31&&version!=32&&version!=33&&version!=34&&version!=35)throw Failure("Unsupported save version");auto size=header.integer(0,8),checksum=header.integer(0,8);auto payload=bytes.subspan(header.pos);if(size!=payload.size())throw Failure("Invalid save payload length");if(hash(payload)!=checksum)throw Failure("Save checksum mismatch");Archive a(payload);a.version=uint32_t(version);SaveGame s;a(s);if(a.pos!=payload.size())throw Failure("Trailing save data");validate(s);out=std::move(s);error.clear();return true;}catch(const std::exception& e){error=e.what();return false;}}
uint64_t catalogFingerprint(const SceneContent& content){auto classes=content.classes;auto paths=content.paths;Archive a;a(classes,paths);return hash(a.bytes);}
bool writeSaveFile(const std::string& path,const SaveGame& s,std::string& error){
 std::vector<uint8_t> bytes;if(!encodeStoredSave(s,bytes,error))return false;
 return platform::atomicWrite(path,bytes,error);
}
bool writeSaveFile(const std::string& path,SaveGame&& s,std::string& error){
 std::vector<uint8_t> bytes;if(!encodeStoredSave(std::move(s),bytes,error))return false;
 return platform::atomicWrite(path,bytes,error);
}
bool readSaveFile(const std::string& path,SaveGame& out,std::string& error){
 std::ifstream stream(path,std::ios::binary|std::ios::ate);if(!stream){error="Cannot open save file";return false;}auto size=stream.tellg();if(size<0||uint64_t(size)>MaxFile){error="Invalid save file size";return false;}std::vector<uint8_t> bytes{ };bytes.resize(size_t(size));stream.seekg(0);if(!bytes.empty()&&!stream.read(reinterpret_cast<char*>(bytes.data()),std::streamsize(bytes.size()))){error="Cannot read complete save file";return false;}return decodeSave(bytes,out,error);
}
} // namespace vyruz
