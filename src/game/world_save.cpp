#include "game/save_state.h"
#include "game/runtime_storage.h"
#include "game/save_file.h"
#include "game/properties.h"
#include "game/projectile_contact_native.h"
#include "game/enemy_idle_native.h"
#include "game/lift_access.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <sstream>

namespace vyruz {
namespace {
struct Fingerprint {
    uint64_t value=14695981039346656037ull;
    void number(uint64_t n) { for(int i=0;i<8;++i){value^=(n>>(8*i))&255;value*=1099511628211ull;} }
    void text(const std::string& s) { number(s.size());for(unsigned char c:s){value^=c;value*=1099511628211ull;} }
    void nodes(const std::vector<TextNode>& ns) {
        number(ns.size());for(const auto& n:ns){number(static_cast<unsigned>(n.kind));text(n.key);
            number(n.arguments.size());for(const auto& s:n.arguments)text(s);
            number(n.values.size());for(const auto& s:n.values)text(s);nodes(n.children);}
    }
    void position(ContentPosition p) { number(std::bit_cast<uint32_t>(p.x));number(std::bit_cast<uint32_t>(p.y)); }
    void sprite(const SpriteReference& s) {auto path=folded(s.path);std::replace(path.begin(),path.end(),'\\','/');text(path);number(s.sequence.value);}
    void flags(const ActorFlags& f) {for(auto v:{f.invisible,f.detail,f.inGround,f.overWater,f.zShadow,f.managed,f.inertia})number(v?(*v?2:1):0);}
    void behaviours(const std::vector<std::string>& b) {number(b.size());for(const auto& s:b)text(s);}
};
bool finite(std::initializer_list<double> values) {return std::all_of(values.begin(),values.end(),[](double n){return std::isfinite(n);});}
bool position(ContentPosition p) {return finite({p.x,p.y});}
bool validCreates(const ActorState& actor,size_t classes,bool local=true) {
    if(!actor.createsInitialized && !actor.creates.empty())return false;
    for(const auto& c:actor.creates)
        if((local && c.classId.value>=classes) || !position(c.offset) ||
           !finite({c.nextMs,c.repeatMs,c.trailStep}) || c.direction<0 || c.direction>=24 ||
           (c.previousOrigin && !position(*c.previousOrigin)))return false;
    return true;
}
}
uint64_t GameWorld::contentFingerprint() const {
    auto path=folded(scene_->mapPath);std::replace(path.begin(),path.end(),'\\','/');
    Fingerprint h;h.text(path);h.text(scene_->levelId);h.number(level_->mapW);h.number(level_->mapH);
    h.number(scene_->classes.size());
    for(const auto& c:scene_->classes){h.number(c.id.value);h.text(c.name);h.number(c.parent?uint64_t(c.parent->value)+1:0);
        h.nodes(c.properties);h.sprite(c.sprite);h.sprite(c.shadow);h.flags(c.flags);h.behaviours(c.behaviours);}
    h.number(scene_->objects.size());
    for(const auto& o:scene_->objects){h.number(o.id.value);h.number(o.classId.value);h.position(o.position);
        h.nodes(o.properties);h.sprite(o.sprite);h.sprite(o.shadow);h.flags(o.flags);h.behaviours(o.behaviours);
        h.nodes(o.authored.children);}
    h.number(scene_->paths.size());for(const auto& p:scene_->paths){h.number(p.id.value);h.number(p.points.size());for(auto q:p.points)h.position(q);}
    return h.value;
}
WorldSnapshot GameWorld::snapshot() const {
    WorldSnapshot s;s.bodyGraphicsStepMs=bodyGraphicsStepMs_;s.actorCells=actorCells_;s.spiderNavigation=spiderNavigation_;s.catalogHash=catalogHash_;s.difficulty=difficulty_.level;s.objects=scene_->objects;s.actors=actors_;s.projectiles=projectiles_;
    s.nativePlayerSpeed=nativePlayerSpeed_;
    s.player=player_;s.navigationCursor=navigationCursor_;s.motion=motion_;s.elapsedMs=elapsedMs_;s.playerKills=playerKills_;s.ammoHits=ammoHits_;s.ammoMisses=ammoMisses_;
    s.shotsFired=shotsFired;s.pickupsTaken=pickupsTaken;s.actorsDestroyed=actorsDestroyed;s.transition=transition_;
    s.rejectedLifts=rejectedLifts_;s.message=message_;std::ostringstream random;random<<random_;s.randomState=random.str();
    s.savePlot=savePlot_;s.saveDwellMs=saveDwellMs_;s.saveAttempted=saveAttempted_;s.surprise=surprise_;s.heldItems=heldItems_;
    s.fastKillMs=fastKillMs_;s.fastKillCount=fastKillCount_;s.bonusEnemies=bonusEnemies_;
    s.nativePool=captureNativePool();
    s.nativeAwake=captureNativeAwake();s.nativePlayerFacing=nativePlayerFacing_;s.nativePlayerPreviousSurface=nativePlayerPreviousSurface_;
    s.nativeReplayRandom=nativeReplayRandom_;
    s.endingSource=endingSource_;s.endingRemainingMs=endingRemainingMs_;s.nativeWorldRandom=nativeWorldRandom_;s.nativeGlobalRandom=nativeGlobalRandom_;
    s.nativeAmmoLifecycleKnown=true;s.bareAmmoCells=bareAmmoCells_;
    s.nextNativeGeneration=nextNativeGeneration_;s.nextNativeCellOrder=nextNativeCellOrder_;s.nextNativeBodyIdentity=nextNativeBodyIdentity_;
    return s;
}
bool GameWorld::restoreSnapshot(const WorldSnapshot& saved,std::string& error) {
    auto fail=[&](const char* message){error=message;return false;};
    if(!scene_ || !level_ || saved.catalogHash!=catalogHash_)return fail("checkpoint content does not match this level");
    if(!std::isfinite(saved.fastKillMs) || saved.fastKillMs<0 || saved.fastKillMs>500 ||
       saved.fastKillCount>1000000 || (saved.bonusEnemies && (*saved.bonusEnemies < -1000000 || *saved.bonusEnemies > 1000000)))return fail("invalid combat bonus state");
    if(!validSpiderNavigationSnapshot(saved))return fail("invalid Spider navigation checkpoint");
    if(saved.nativePool)for(const auto& held:saved.heldItems) {
        const auto id=held.object.id.value;
        if(id<saved.actors.size() && held.actor.nativePoolSlot!=SIZE_MAX &&
           held.actor.nativePoolSlot==saved.actors[id].nativePoolSlot) {
            auto origin=folded(held.mapPath),current=folded(scene_->mapPath);
            std::replace(origin.begin(),origin.end(),'\\','/');std::replace(current.begin(),current.end(),'\\','/');
            if(origin!=current)return fail("foreign held item aliases a local pool body");
        }
    }
    if(saved.actorCells && (saved.actorCells->width!=level_->mapW || saved.actorCells->height!=level_->mapH))return fail("invalid actor cell dimensions");
    if(!validMissileNativeNavigationSnapshot(saved))return fail("invalid native missile navigation checkpoint");
    if(saved.spiderNavigation)for(const auto& o:saved.spiderNavigation->objects)
        if(o.alive&&!o.point&&(o.reference&(uint64_t(1)<<63))&&
           (o.reference&~(uint64_t(1)<<63))>=level_->nativeNavigationDecor.size())return fail("invalid Spider decor reference");
    DifficultyPolicy restoredDifficulty;
    if(!loadDifficultyPolicy(*scene_,saved.difficulty,restoredDifficulty,error))return false;
    const auto count=saved.objects.size();
    if(!std::isfinite(saved.endingRemainingMs) || saved.endingRemainingMs<0 || saved.endingRemainingMs>4500 ||
       (!saved.endingSource && saved.endingRemainingMs!=0) ||
       (saved.endingSource && (*saved.endingSource>=count || !propertyBool(saved.objects[*saved.endingSource].properties,"Final-object"))))return fail("invalid ending countdown");
    if(!count || count>100000 || saved.actors.size()!=count || saved.player!=player_ || saved.player>=count ||
       saved.objects[saved.player].classId!=scene_->objects[player_].classId)return fail("invalid checkpoint object catalog");
    if(!finite({saved.elapsedMs,saved.motion.turnSeconds,saved.motion.velocityX,saved.motion.velocityZ,saved.motion.magnitude,saved.motion.thrust,saved.saveDwellMs}) ||
       saved.elapsedMs<0 || saved.elapsedMs>1e15 || saved.motion.magnitude<0 || saved.motion.direction<0 || saved.motion.direction>=24 ||
       saved.saveDwellMs<0 || saved.saveDwellMs>5500 ||
       (!saved.savePlot && (saved.saveDwellMs!=0 || saved.saveAttempted)))return fail("invalid checkpoint clock or motion");
    std::mt19937 restoredRandom;std::istringstream random(saved.randomState);
    // MSVC's random-engine extractor throws on corrupt input; other standard
    // libraries set failbit. Both must reject transactionally through this API.
    try {
        if(!(random>>restoredRandom))return fail("invalid checkpoint random state");
    } catch(const std::exception&) {
        return fail("invalid checkpoint random state");
    }
    const auto& effect=saved.surprise;
    for(float height:effect.orbitAttachmentAltitudes)if(!std::isfinite(height)||std::trunc(height)!=height)
        return fail("invalid attachment altitude");
    for(const auto& p:effect.bananaOrbitPositions)if(!position(p))return fail("invalid attached orbit history");
    if(static_cast<unsigned>(effect.kind)>20 || effect.kind==SurpriseKind::Random ||
       !finite({effect.delayMs,effect.remainingMs,effect.tripleOffset,effect.feedbackMs,effect.feedbackStrength}) ||
       !position(effect.droneOrigin) || !position(effect.droneReturnFrom) || !position(effect.shakeOffset) ||
       effect.feedbackMs<0 || effect.feedbackStrength<0 || effect.feedbackStrength>150 ||
       !std::isfinite(effect.bananaTrailNextMs) || effect.bananaTrailNextMs<0 ||
       effect.delayMs>500 || effect.remainingMs>20000 || effect.circularRemaining<0 ||
       effect.circularRemaining>24 || effect.timeWarpPhase<0 || effect.timeWarpPhase>3 ||
       effect.tripleOffset<0 || effect.tripleOffset>400 ||
       (effect.kind==SurpriseKind::None && effect.applied))return fail("invalid checkpoint Surprise state");
    random>>std::ws;if(!random.eof())return fail("invalid checkpoint random state suffix");
    std::set<std::pair<std::string,uint32_t>> heldSources;
    for(const auto& held:saved.heldItems) {
        const auto& actor=held.actor;
        if(held.holder>=count || held.className.empty() || !position(held.object.position) ||
           !propertyBool(held.object.properties,"Carry") || !attribute(held.object.properties,"item") ||
           (!held.mapPath.empty() && !validSaveLevelPath(held.mapPath)) ||
           !heldSources.emplace(folded(held.mapPath),held.object.id.value).second ||
           (actor.enemyMoveCommand!=0 && actor.enemyMoveCommand!=200 && actor.enemyMoveCommand!=208) || actor.direction<0 || actor.direction>=24 || !actor.pickup || !actor.alive ||
           !finite({actor.enemyThrust,actor.enemyMagnitude,actor.ageMs,actor.vitals.energy,actor.vitals.shield,actor.reactivateMs,actor.contactVelocity.x,actor.contactVelocity.z}) ||
           (actor.shieldUpStep && !finite({*actor.shieldUpStep})) || actor.ageMs<0 ||
           actor.enemyMagnitude<0 || (actor.driveSegmentStart && !position(*actor.driveSegmentStart)) ||
           (actor.driveSegmentEnd && !position(*actor.driveSegmentEnd)) ||
           actor.pooledTrail || actor.explosion || !actor.heldKeys.empty() ||
           (actor.weapons.empty()?actor.selectedWeapon!=0:actor.selectedWeapon>=actor.weapons.size()) ||
           !finite({actor.radius,actor.speed,actor.lifeMs,actor.pickupDelayMs,actor.vitals.energyMax,actor.vitals.shieldMax}) ||
           actor.radius<0 || actor.speed<0 || (actor.itemTimeoutMs && !finite({*actor.itemTimeoutMs})))return fail("invalid held item state");
        if(!validCreates(actor,scene_->classes.size(),folded(held.mapPath)==folded(scene_->mapPath)))return fail("invalid held CREATE state");
        for(const auto& weapon:actor.weapons)if(!finite({weapon.state.slopeRadians}) || (weapon.ammoSlot &&
            (*weapon.ammoSlot>=actor.ammo.size() || actor.ammo.slots[*weapon.ammoSlot].first!=weapon.ammo)))return fail("invalid held ammunition slot");
        if(folded(held.mapPath)==folded(scene_->mapPath) &&
           (held.object.id.value>=count || saved.actors[held.object.id.value].alive ||
            held.object.classId!=saved.objects[held.object.id.value].classId || held.object.id.value==held.holder ||
            held.object.classId.value>=scene_->classes.size() ||
            !contentNameEqual(held.className,scene_->classes[held.object.classId.value].name)))return fail("invalid held source identity");
    }
    if(saved.bodyGraphicsStepMs<0 || saved.bodyGraphicsStepMs>101)return fail("Invalid body graphics elapsed step");
    size_t graphicBodies=0;for(const auto& a:saved.actors)graphicBodies+=bool(a.bodyGraphics);
    if(graphicBodies>(std::numeric_limits<uint64_t>::max()-nextBodyGraphicsIdentity_)/24)return fail("Body graphics audio identity exhausted");
    auto actors=saved.actors;
    // Transient hit/engine presentation is deliberately excluded from checkpoints.
    for(auto& actor:actors){actor.visualThrust=actor.engineGlowMs=actor.hitFlashMs=actor.shieldPulse=0;actor.headlightVisible=true;actor.locomoting=false;actor.ammoHidden=false;actor.ammoContactDisabled=false;actor.ammoAlpha=false;actor.ammoVisibility=49;actor.ammoFadeClockMs=0;actor.nativeAmmoAbort=false;}
    for(size_t i=0;i<count;++i) {
        const auto& o=saved.objects[i];auto& a=actors[i];const auto& v=a.vitals;const auto& b=a.brain;
        if(a.nativeDamagePending && ((!a.alive && a.reactivateMs<0) || v.energy>0))return fail("invalid pending Ammo death");
        if(a.nativeAmmoContinuation && (!a.nativeMotionHistory() || (!a.alive && a.reactivateMs<0)))return fail("invalid pending Ammo movement");
        if(o.id.value!=i || o.classId.value>=scene_->classes.size() || (o.owner&&o.owner->value>=count) || !position(o.position))return fail("invalid checkpoint object reference");
        a.nativeSubtype=nativeObjectSubtype(o,i==saved.player);
        a.bodyGraphicsGraphicUpdate=propertyBool(scene_->classes[o.classId.value].properties,"Graphic-update-needed");
        try {if(!validateBodyGraphics(a,o,error))return false;}
        catch(const std::runtime_error& failure){return fail(failure.what());}
        a.hasAmmoAttribute=attribute(o.properties,"ammo")!=nullptr;
        a.visibleWhenFiring=propertyBool(o.properties,"Visible-when-firing");
        if((a.enemyMoveCommand!=0 && a.enemyMoveCommand!=200 && a.enemyMoveCommand!=208) || a.direction<0 || a.direction>=24 || (a.weapons.empty()?a.selectedWeapon!=0:a.selectedWeapon>=a.weapons.size()) ||
           a.routePoint>a.route.size() || (b.targetClass&&b.targetClass->value>=scene_->classes.size()) ||
           static_cast<unsigned>(b.base)>static_cast<unsigned>(BrainBase::Unsupported))return fail("invalid checkpoint actor reference");
        for(const auto& weapon:a.weapons)
            if(weapon.ammoSlot && (*weapon.ammoSlot>=a.ammo.size() || a.ammo.slots[*weapon.ammoSlot].first!=weapon.ammo))
                return fail("invalid checkpoint ammo slot");
        if((a.shieldUpStep && !finite({*a.shieldUpStep})) || !finite({a.enemyThrust,a.enemyMagnitude,a.contactVelocity.x,a.contactVelocity.z,a.radius,a.speed,a.turnMs,a.lifeMs,a.ageMs,a.pickupDelayMs,a.reactivateMs,a.routeRetryMs,a.randomWaitMs,
           v.energy,v.energyMin,v.energyMax,v.shield,v.shieldMin,v.shieldMax,a.pickupEffect.energy,a.pickupEffect.shield,
           b.sensorRange,b.minimumDistance,b.scanDelayMs,b.visionDegrees,b.guardianRange,b.home.x,b.home.y,b.center.x,b.center.y,
           b.randomRange,b.randomDelayMs,a.brainState.scanRemainingMs,a.gateState.remainingMs}) || a.enemyMagnitude<0 || a.radius<0 || a.speed<0 || a.ageMs<0 ||
           (a.driveSegmentStart && !position(*a.driveSegmentStart)) || (a.driveSegmentEnd && !position(*a.driveSegmentEnd)))
            return fail("invalid checkpoint actor numbers");
        if(a.nativeMotion && (!position(a.nativeMotion->previousSurface)||!finite({a.nativeMotion->center.x,a.nativeMotion->center.y})))return fail("invalid native contact history");
        const auto boundsSubtype=objectSubtype(o);
        const bool square=boundsSubtype=="gate"||boundsSubtype=="lift"||
            (boundsSubtype.empty()&&(attribute(o.properties,"gate")||attribute(o.properties,"lift")));
        if((square&&a.radius>=2147483648.f)||
           (a.nativeGeneratorSquare&&(!square||!validNativeGeneratorSquare(*a.nativeGeneratorSquare))))return fail("invalid native generator square bounds");
        // v1-v34 omitted polygon history. Reconstruct the constructor bounds
        // once at the saved surface; historical rounding cannot be recovered.
        if(square&&!a.nativeGeneratorSquare)a.nativeGeneratorSquare=makeNativeGeneratorSquare(o.position.x,o.position.y,static_cast<int32_t>(a.radius));
        if(a.spiderAI && !validSpiderAI(*a.spiderAI,count))return fail("invalid Spider AI checkpoint state");
        if(a.spiderAI && a.spiderAI->turn &&
           (objectSubtype(o)!="spider" || a.spiderAI->turn->bodyPose-1!=a.direction))return fail("invalid Spider turn pose");
        configureContact(a,o);configureWeaponSlopes(a);
        configureEnemyTurning(a,o,true);
        if(a.ordinaryAI && (!a.alive && a.reactivateMs<0))return fail("invalid ordinary native AI family");
        configureOrdinaryEnemyAI(a,o,true);
        if(saved.actors[i].ordinaryAI && !a.ordinaryAIConfig.enabled)return fail("unsupported ordinary native AI family");
        if(a.enemyTurn) {
            const auto& t=*a.enemyTurn;const int ring=enemyTurnPoseRing(a.enemyTurnConfig.subtype);
            if(!a.enemyTurnConfig.subtype || t.logicalHeading!=a.direction ||
               ((!a.enemyTurnConfig.twoPieces && !supportsOnePieceEnemyIdle(a.enemyTurnConfig)) &&
                t.logicalHeading!=(t.bodyPose-1)/enemyTurnPoseMultiplier(a.enemyTurnConfig.subtype)) ||
               t.orthogonalHeading!=(t.logicalHeading+3)%24 ||
               t.bodyPose<1 || t.bodyPose>ring || t.bodyPreviousPose<1 || t.bodyPreviousPose>ring ||
               t.bodyDesiredPose<1 || t.bodyDesiredPose>ring ||
               t.gunPose<1 || t.gunPose>ring || t.gunPreviousPose<1 || t.gunPreviousPose>ring ||
               t.gunDesiredPose<1 || t.gunDesiredPose>ring ||
               (t.turnCommand!=0 && t.turnCommand!=0xcb && t.turnCommand!=0xcd) ||
               t.gunLogicalHeading<0 || t.gunLogicalHeading>=24 ||
               (t.gunCommand!=0 && t.gunCommand!=203 && t.gunCommand!=205) ||
               (t.gunPreviousCommand!=0 && t.gunPreviousCommand!=203 && t.gunPreviousCommand!=205) ||
               t.lean< -15 || t.lean>15 || t.bodyRemainingMs<0 || t.gunRemainingMs< -100000000 ||
               (!a.enemyTurnConfig.twoPieces && t.gunRemainingMs<0) ||
               t.bodyRemainingMs>100000000 || t.gunRemainingMs>100000000 ||
               t.bodyPeriodMs< -100000000 || t.bodyPeriodMs>100000000 ||
               t.gunPeriodMs< -100000000 || t.gunPeriodMs>100000000)
                return fail("invalid native enemy turning state");
        }
        const auto family=objectSubtype(o);
        a.turretOnlyBase=(family=="turret" || family=="tank") && propertyBool(o.properties,"Only-base");
        if(a.turretOnlyBase){a.brain.base=BrainBase::Fixed;a.brain.seeker=a.brain.tracker=false;a.hudEnemy=false;}
        configureShieldRegeneration(a,o,restoredDifficulty,i==saved.player,true);
        // Pre-v30 active memory snapshots had a separate shield producer.
        if(!saved.actors[i].shieldUpStep && i==saved.player && saved.surprise.active(SurpriseKind::Shield))
            a.shieldUpStep=a.shieldUpStep.value_or(0)+10.f;
        if(a.pooledTrail && (a.solid || a.explosion || a.generatorParent || !reusableTrail(o.classId)))return fail("invalid pooled trail");
        if(!validCreates(a,scene_->classes.size()))return fail("invalid ordinary CREATE state");
        if(!a.createsInitialized)configureActorCreates(a,o,true);
        if(!a.generatorsInitialized)configureGenerators(a,o);
        if(a.generatorParent && (*a.generatorParent>=count || *a.generatorParent==i))return fail("invalid generator parent");
        for(const auto& g:a.generators)
            if((g.product && g.product->value>=scene_->classes.size()) || !finite({g.delayMs,g.durationMs,g.remainingMs}))
                return fail("invalid generator state");
        if(a.explosion) {
            const auto& e=*a.explosion;
            if(!a.effect || (e.creator && *e.creator>=count) || !finite({e.damage,e.radius,e.remainingMs,e.altitude,e.animation.remaining}) ||
               e.radius<0 || e.remainingMs>500 || e.remainingMs< -1)return fail("invalid checkpoint Explosion");
            if(e.initialized) {
                const auto* sequence=effectSequence(o.sprite);AnimationCursor cursor;
                if(!sequence || !cursor.restore(*sequence,e.animation))return fail("invalid checkpoint Explosion animation");
            }
        }
        if(a.projectileAltitude && !finite({*a.projectileAltitude}))return fail("invalid checkpoint altitude");
        if(a.trailSegment && (!a.effect || !position(a.trailSegment->previous) ||
            !finite({a.trailSegment->step}) || a.trailSegment->step<=0))return fail("invalid checkpoint FX trail");
        if((a.routeGoal&&!position(*a.routeGoal)) || (a.randomGoal&&!position(*a.randomGoal)))return fail("invalid checkpoint route goal");
        for(auto p:a.route)if(!position(p))return fail("invalid checkpoint route point");
        if(a.routeSearch && !a.routeSearch->restore(*level_,a.routeSearch->capture()))return fail("invalid checkpoint navigation search");
        for(const auto& w:a.weapons)if(!finite({w.config.weaponsGap,w.range,w.state.slopeRadians}) || w.config.weaponsGap<0 ||
            (w.state.lastFireMs&&*w.state.lastFireMs>saved.elapsedMs))return fail("invalid checkpoint weapon state");
        for(const auto& k:a.heldKeys){
            if(k.part<1 || k.total<1 || k.part>k.total)return fail("invalid checkpoint key part");
            if(k.held) {
                const auto& held=*k.held;const auto* key=attribute(held.object.properties,"key");
                if(!position(held.object.position) || !finite({held.remainingMs,held.classTimeoutMs}) ||
                   held.className.empty() || (!held.mapPath.empty() && !validSaveLevelPath(held.mapPath)) || !key ||
                   propertyBool(key->children,"Is-code")!=k.code || propertyNumber(key->children,"Id")!=k.id ||
                   propertyNumber(key->children,"Code-part-id",1)!=k.part ||
                   propertyNumber(key->children,"Code-part-count",1)!=k.total ||
                   propertyText(key->children,"Code-String")!=k.text)return fail("invalid held code state");
            }
        }
        if(a.itemTimeoutMs && !finite({*a.itemTimeoutMs}))return fail("invalid item timer");
        if(a.lift && (!attribute(o.properties,"lift") || !finite({a.lift->delayMs,a.liftState.remainingMs}) ||
           a.lift->delayMs<0 || a.liftState.remainingMs<0 || !a.lift->baseSequence.value || !a.liftState.sequence.value ||
           (a.lift->flags&~uint32_t{511}) || (a.liftState.flags&~uint32_t{511}) || !a.gateRevision))
            return fail("invalid checkpoint Lift");
        if(!a.lift) {
            // Saves before v14 had no Lift controller. Resume from the authored
            // startup pose/countdown; elapsed actor age was not a cycle state.
            a.lift=makeLiftConfig(o);
            if(a.lift){a.liftState=initializeLift(*a.lift);a.gateRevision=1;a.gateAnimationFinished=false;}
        }
        if(a.lift)a.solid=a.alive&&!a.liftState.passable;
        if(a.gate && (!finite({a.gate->delayMs}) || !a.gate->baseSequence.value || !a.gateState.sequence.value ||
           (a.gateState.flags&~uint32_t{511})))return fail("invalid checkpoint gate");
    }
    for(const auto& p:saved.projectiles) {
        const auto& m=p.motion;const auto& c=p.motionConfig;
        if((p.owner!=SIZE_MAX && p.owner>=count) || (p.visual&&*p.visual>=count) || p.classId.value>=scene_->classes.size() ||
           !finite({p.slopeRadians,p.slopeHeight,p.slopeAltitude,p.position.x,p.position.y,p.velocity.x,p.velocity.y,p.directionX,p.directionZ,p.radius,p.damage,p.damageRadius,
            p.distance,p.range,p.ageMs,p.lifetimeMs,p.activeAfterMs,p.fuseMs,p.inactiveMs,p.onMs,p.damageRemainingMs,m.velocityX,m.velocityZ,m.magnitude,m.thrust,
            c.speedMax,c.acceleration,c.deceleration,c.weight,c.grip,c.frictionX,c.frictionZ,c.thresholdX,c.thresholdZ}) || p.radius<0 || p.ageMs<0)
            return fail("invalid checkpoint projectile");
        const auto& guidance=p.guidance;
        if((p.guidanceClass && p.guidanceClass->value>=scene_->classes.size()) ||
           (p.guidanceTarget && *p.guidanceTarget>=count) ||
           static_cast<unsigned>(guidance.phase)>static_cast<unsigned>(MissileGuidancePhase::Following) ||
           guidance.pose48<1 || guidance.pose48>48 || p.guidancePoint>p.guidanceRoute.size() ||
           !position(p.guidanceStart) || !finite({guidance.sampledTarget.x,guidance.sampledTarget.y}))
            return fail("invalid checkpoint missile guidance");
        for(int timer:{guidance.scanRemainingMs,guidance.pathRemainingMs,guidance.turnRemainingMs})
            if(timer< -100000000 || timer>100000000)return fail("invalid checkpoint missile timer");
        for(const auto point:p.guidanceRoute)if(!position(point))return fail("invalid checkpoint missile route");
        if(p.guidanceSearch){NavigationSearch search;if(!search.restore(*level_,p.guidanceSearch->capture()))return fail("invalid checkpoint missile search");}
        for(const auto& create:p.creates)if(create.classId.value>=scene_->classes.size() ||
            !position(create.offset) || (create.previousOrigin && !position(*create.previousOrigin)) ||
            !finite({create.nextMs,create.repeatMs,create.trailStep}) || create.repeatMs<0 || create.trailStep<0)
            return fail("invalid checkpoint projectile CREATE");
        if(p.inactiveMs< -1 || p.onMs<0 || p.damageRemainingMs<0)return fail("invalid checkpoint ammo phase");
    }
    if(saved.transition && (saved.transition->lift>=count || saved.transition->cost<0 ||
       (!attribute(saved.objects[saved.transition->lift].properties,"lift") &&
        !(saved.transition->path=="*" && propertyBool(saved.objects[saved.transition->lift].properties,"Final-object")))))return fail("invalid checkpoint Lift request");
    for(size_t i=0;i<saved.actors.size();++i)if(saved.timedProtectionKnown) {
        const auto config=timedProtectionConfig(*scene_,saved.objects[i]);const auto& p=saved.actors[i].timedProtection;
        if(!finite({p.onRemainingMs,p.offRemainingMs}) || p.onRemainingMs < -100 || p.offRemainingMs < -100 ||
           p.onRemainingMs>config.onMs || p.offRemainingMs>config.offMs ||
           (config.onMs<=0 && (p.active || p.onRemainingMs!=0 || p.offRemainingMs!=0)))return fail("invalid timed protection");
    }
    for(auto i:saved.rejectedLifts)if(i>=count || !attribute(saved.objects[i].properties,"lift"))return fail("invalid checkpoint rejected Lift");
    if(saved.savePlot && (*saved.savePlot>=count || !propertyBool(saved.objects[*saved.savePlot].properties,"Saving-plot")))return fail("invalid checkpoint saving terminal");
    // Validate and allocate everything before publishing any part of the restore.
    auto objects=saved.objects;auto projectiles=saved.projectiles;auto rejected=saved.rejectedLifts;auto transition=saved.transition;auto message=saved.message;
    // Older port saves assigned the shooter as an inert CREATE trail's owner.
    // Native437b00 creates these independently. Derive the role from authored
    // Ammo CREATE targets, also covering pre-v11 trails without pooling flags.
    // Keep damaging Ammo/Explosion and generator ancestry intact.
    std::set<uint32_t> detachedTrails;
    for(const auto& cls:scene_->classes)if(attribute(cls.properties,"ammo"))
        for(const auto& node:cls.properties)if(contentNameIs(node.key,"create"))
            if(const auto* target=findClass(*scene_,propertyText(node.children,"Name"));
               target && reusableTrail(target->id))detachedTrails.insert(target->id.value);
    for(size_t i=0;i<objects.size();++i)
        if(detachedTrails.contains(objects[i].classId.value) && !actors[i].solid &&
           !actors[i].explosion && !actors[i].generatorParent)objects[i].owner.reset();
    if(message=="Saving completed" || message=="Saving cancelled" || message=="Saving..." ||
       message=="Hold position to save" || message=="Insufficient credits to save" ||
       message.starts_with("Saving in ") || message.starts_with("Cannot save: "))message.clear();
    auto surprise=saved.surprise;
    reserveRuntimeStorage(actors);reserveRuntimeStorage(objects);
    difficulty_=restoredDifficulty;
    scene_->objects=std::move(objects);actors_=std::move(actors);projectiles_=std::move(projectiles);
    bodyGraphicsStepMs_=saved.bodyGraphicsStepMs;
    bodyGraphicsAudio_.clear();bodyGraphicsAudioPins_.clear();
    for(auto& actor:actors_)if(actor.bodyGraphics){
        NativeBodyGraphicsIdentities ids;
        if(nativeBodyGraphicsIdentities(nextBodyGraphicsIdentity_,ids)){
            nextBodyGraphicsIdentity_+=ids.size();nativeBodyGraphicsRestoreAudio(*actor.bodyGraphics,ids);
        }
    }
    nextNativeGeneration_=saved.nativeAmmoLifecycleKnown?saved.nextNativeGeneration:1;
    nextNativeCellOrder_=saved.nativeAmmoLifecycleKnown?saved.nextNativeCellOrder:1;
    nextNativeBodyIdentity_=saved.nativeAmmoLifecycleKnown?saved.nextNativeBodyIdentity:1;
    // Validation above used the currently bound difficulty. Rebuild this
    // derived cache once more with the restored policy, preserving native
    // actions and graph handles; old A* state has already been replanned.
    for(size_t id=0;id<actors_.size();++id)configureOrdinaryEnemyAI(actors_[id],scene_->objects[id],true);
    if(saved.actorCells)actorCells_=*saved.actorCells;else initializeActorCells();
    reserveRuntimeStorage(actorCells_.cell);reserveRuntimeStorage(actorCells_.next);reserveRuntimeStorage(actorCells_.arrivalOrder);
#if VURUS_COLLISION_INDEX_ORACLE
    reserveRuntimeStorage(actorBounds_);reserveRuntimeStorage(actorBoundGroup_);reserveRuntimeStorage(actorBoundOrder_);
    reserveRuntimeStorage(actorBoundGroups_,(RuntimeActorLimit+15)/16);
#endif
    collisionMaximumRadius_=0;collisionCoverageCount_=0;collisionQueriesActive_=false;
    collisionEligible_.clear();collisionUnlocated_.clear();
    if(saved.nativeAmmoLifecycleKnown)bareAmmoCells_=saved.bareAmmoCells;
    else {
        nextNativeCellOrder_=std::max(nextNativeCellOrder_,actorCells_.nextArrivalOrder);
        bareAmmoCells_.reset(actorCells_.width,actorCells_.height);
        for(size_t id=0;id<actors_.size();++id) {
            initializeNativeIdentity(id);
            if(!actors_[id].alive && actors_[id].reactivateMs<0)actors_[id].nativeGeneration=0;
        }
        for(size_t id=0;id<actors_.size();++id) {
            const auto owner=scene_->objects[id].owner;
            assignNativeCreator(id,owner?std::optional<size_t>{owner->value}:std::nullopt);
        }
        // Historical owner hit/miss words were never saved and cannot be
        //recovered from the distinct global Ammo counters. Keep them zero.
        actors_[saved.player].nativeAmmoOwner.kills=static_cast<uint32_t>(saved.playerKills);
        for(auto& shot:projectiles_) {
            initializeBareAmmoBody(shot);
            if(shot.alive)if(const auto owner=nativeAmmoOwner(shot))nativeAmmoOwnerLaunched(actors_[*owner].nativeAmmoOwner);
            synchronizeBareAmmoCell(shot);
        }
    }
    rebuildBareAmmoIndex();
    spiderNavigation_=saved.spiderNavigation;
    if(spiderNavigation_)restoreSpiderNavigationIndex();else initializeSpiderNavigation();
    if(!saved.nativeAmmoLifecycleKnown)for(auto& shot:projectiles_)updateSpiderProjectileNavigation(shot);
    // Persistent FX altitude survives; bounce bodies derive their current height.
    for(auto& shot:projectiles_) {
        shot.phaseConfigReady=false;
        if(shot.visual && shot.guidanceClass)actors_[*shot.visual].direction=(shot.guidance.pose48-1)/2;
        if(shot.visual){auto& actor=actors_[*shot.visual];actor.ammoHidden=shot.phaseHidden;actor.ammoContactDisabled=!ammoContactEnabled(shot);actor.ammoVisibility=shot.phaseHidden?0:49;actor.ammoAlpha=shot.phaseHidden || (shot.onMs>0 && shot.onMs<800);}
        if(shot.visual) {
            auto& height=actors_[*shot.visual].projectileAltitude;
            // v6+ preserves actual launch altitude; do not replace it with the
            // ammo class height. Old saves need the original shooter/weapon rule.
            if(!height) {
                const auto& ammo=scene_->classes[shot.classId.value];
                height=propertyNumber(ammo.properties,"Altitude");
                if(shot.owner<actors_.size()) {
                    const auto& shooter=actors_[shot.owner];
                    height=shooter.projectileAltitude.value_or(propertyNumber(scene_->objects[shot.owner].properties,"Altitude"));
                    for(const auto& weapon:shooter.weapons) {
                        const auto* cls=findClass(*scene_,weapon.name);if(!cls)continue;
                        const bool compatible=std::any_of(cls->properties.begin(),cls->properties.end(),[&](const auto& node){
                            return folded(node.key)=="type"&&!node.arguments.empty()&&folded(node.arguments.front())=="weapon"&&
                                folded(propertyText(node.children,"Ammo"))==folded(ammo.name);
                        });
                        if(!compatible)continue;
                        for(auto it=cls->properties.rbegin();it!=cls->properties.rend();++it)
                            if(folded(it->key)=="type"&&!it->arguments.empty()&&folded(it->arguments.front())=="weapon") {
                                const float explicitHeight=propertyNumber(it->children,"Fire-altitude");
                                if(explicitHeight!=0)height=explicitHeight;
                                break;
                            }
                        break;
                    }
                }
                if(ammo.flags.inGround.value_or(false))height=0;
            }
            // New snapshots retain stale creator generations exactly; do not
            //recapture them as valid merely because the port ID still exists.
            if(shot.owner<actors_.size())scene_->objects[*shot.visual].owner=ObjectId{static_cast<uint32_t>(shot.owner)};
            else scene_->objects[*shot.visual].owner.reset();
        }
        if(shot.creates.empty())configureProjectileCreates(shot);
        else if(std::any_of(shot.creates.begin(),shot.creates.end(),[](const auto& c){return c.trailStep==0 && !c.previousOrigin;})) {
            // Version6 saved CREATE schedules but had no trail interpolation
            // fields. Rebuild only class-derived step, retaining its schedule.
            auto retained=shot.creates;configureProjectileCreates(shot);
            if(retained.size()==shot.creates.size())for(size_t i=0;i<retained.size();++i)
                if(retained[i].classId==shot.creates[i].classId)retained[i].trailStep=shot.creates[i].trailStep;
            shot.creates=std::move(retained);
        }
        const auto& properties=scene_->classes[shot.classId.value].properties;
        shot.verticalBounce=propertyBool(properties,"Vertical-Bounce");
        shot.bouncePeriod=propertyNumber(properties,"Bounce-period");
        const auto* ammo=attribute(properties,"ammo");
        shot.terrainBounce=ammo && nativeAmmoTerrainResponse(propertyBool(ammo->children,"on-hit"),propertyBool(ammo->children,"on-object-hit"));
        shot.bounceElasticity=propertyNumber(properties,"Elasticity");
        if(shot.verticalBounce && shot.visual && shot.ageMs>0)
            actors_[*shot.visual].projectileAltitude=projectileBounceAltitude(
                shot.motion.magnitude,shot.ageMs,shot.bouncePeriod);
    }
    // Older checkpoints saved visible terminals as nonsolid sensors. Derive
    // these two authored roles again without changing the checkpoint layout.
    freeTransientRecords_.clear();rebuildTransientRecordCandidates();nextRecordOrder_=actors_.size();
    for(size_t i=0;i<actors_.size();++i)nextRecordOrder_=std::max(nextRecordOrder_,actorRecordOrder(actors_[i],i)+1);
    freeTrailSlots_.clear();
    contactMines_.clear();
    for(size_t i=0;i<actors_.size();++i) {
        const auto& object=scene_->objects[i];
        if(actors_[i].pooledTrail && !actors_[i].alive)freeTrailSlots_[object.classId.value].insert(i);
        const auto* ammo=attribute(object.properties,"ammo");
        actors_[i].contactMine=ammo && propertyNumber(object.properties,"Speed-max")==0 && propertyNumber(ammo->children,"damage")==0;
        if(actors_[i].contactMine)contactMines_.push_back(i);
        const bool zone=propertyBool(object.properties,"Saving-zone");
        if(zone || propertyBool(object.properties,"Saving-plot")) {
            actors_[i].pickup=false;
            actors_[i].solid=!zone && actors_[i].alive && actors_[i].radius>0 &&
                (!object.flags.invisible.value_or(false) || propertyBool(object.properties,"Keep-bb-when-invisible"));
        }
    }
    player_=saved.player;navigationCursor_=saved.navigationCursor;motion_=saved.motion;elapsedMs_=saved.elapsedMs;playerKills_=saved.playerKills;ammoHits_=saved.ammoHits;ammoMisses_=saved.ammoMisses;
    configurePlayerMotion();debugEnemyAI_=true;debugGodMode_=false;
    ammoContactVictims_.clear();
    shotsFired=saved.shotsFired;pickupsTaken=saved.pickupsTaken;actorsDestroyed=saved.actorsDestroyed;
    transition_=std::move(transition);rejectedLifts_=std::move(rejected);message_=std::move(message);random_=restoredRandom;nativeWorldRandom_=saved.nativeWorldRandom;nativeGlobalRandom_=saved.nativeGlobalRandom;nativeReplayRandom_=saved.nativeReplayRandom;
    restoreNativeAwake(saved.nativeAwake);
    nativePlayerFacing_=saved.nativePlayerFacing.value_or(initializeNativePlayerFacing(motion_.direction));
    nativePlayerSpeed_=saved.nativePlayerSpeed;
    if(nativePlayerSpeed_)nativePlayerFacing_.periodMs=nativePlayerSpeed_->turnPeriodMs;
    nativePlayerPreviousSurface_=saved.nativePlayerFacing?saved.nativePlayerPreviousSurface:playerPosition();
    savePlot_=saved.savePlot;saveDwellMs_=saved.saveDwellMs;saveAttempted_=saved.saveAttempted;saveRequest_.reset();
    surprise_=std::move(surprise);
    if(surprise_.applied && !surprise_.orbitAltitudeInitialized)initializeSurpriseOrbitAltitude();
    fastKillMs_=saved.fastKillMs;fastKillCount_=saved.fastKillCount;
    endingSource_=saved.endingSource;endingRemainingMs_=saved.endingRemainingMs;
    attachedProtectionActors_.clear();
    for(size_t i=0;i<actors_.size();++i) {
        const auto config=timedProtectionConfig(*scene_,scene_->objects[i]);
        if(config.onMs>0&&config.offMs>0)attachedProtectionActors_.push_back(i);
        actors_[i].directFireOnly=propertyBool(scene_->objects[i].properties,"Player-direct-fire-only");
        actors_[i].finalObject=propertyBool(scene_->objects[i].properties,"Final-object");
        if(saved.timedProtectionKnown)actors_[i].timedProtection.config=config;
        else actors_[i].timedProtection.reset(config);
    }
    bonusEnemies_=saved.bonusEnemies.value_or(std::count_if(actors_.begin(),actors_.end(),[](const auto& a){return a.hudEnemy&&(a.alive||a.reactivateMs>=0);}));
    actors_[player_].shieldPulse=surprise_.active(SurpriseKind::Shield)?shieldRegenPulse(elapsedMs_):0;
    heldItems_=saved.heldItems;
    migrateHeldCodes();migrateHeldItems();
    if(!saved.heldItemsKnown)diagnostics_.push_back("Legacy checkpoint: generic carried objects were not recorded; retained key sources recovered where available");
    damageQueue_.clear();touchVictims_.clear();audioEvents_.clear();audioInput_.reset();audioMotorAttempt_=false;playerMoveCommanded_=false;notifications_.clear();playerLocked_=false;
    for(size_t i=0;i<actors_.size();++i)registerSavingPrice(i);
    if(!restoreNativePool(saved.nativePool,error))return false;
    rebuildNativeHeldOriginals();
    retaliationTarget_=SIZE_MAX;retaliationGeneration_=0;
    navigationItemMetadata_.clear();classNames_.clear();savingRoles_.reset();
    resetSpatialPublications();
    applyingDamage_=false;map_.close();error.clear();return true;
}
void GameWorld::registerSavingPrice(size_t id) {
    const auto& object=scene_->objects[id];
    const double reward=propertyNumber(object.properties,"Reward");
    if(reward>=0)return;
    const auto cost=static_cast<int64_t>(std::clamp(-reward,0.0,static_cast<double>(std::numeric_limits<int64_t>::max()/2)));
    notifications_.savingPrice(id,cost,object.position.x,object.position.y);
}
void GameWorld::updateSaving(float milliseconds) {
    const auto playerPos=playerPosition();
    for(const auto& price:notifications_.savingPrices()) {
        const auto id=price.actor;const auto pos=scene_->objects[id].position;
        // 4406e0 uses map-cell deltas, independently of the smaller save sensor.
        const bool near=std::abs(int(pos.x/64)-int(playerPos.x/64))<3 &&
                        std::abs(int(pos.y/32)-int(playerPos.y/32))<4;
        notifications_.advanceSavingPrice(id,actors_[id].alive && near,milliseconds);
    }
    // A background checkpoint owns this request until commit/failure. Moving
    // away must not cancel its fee/notification or start a second transaction.
    if(saveRequest_)return;
    savingRoles_.ensure(*scene_,actors_.size());
    std::optional<size_t> plot;
    if(player().alive && !scene_->objects[player_].flags.invisible.value_or(false)) {
        const auto p=playerPosition();
        for(const size_t area:savingRoles_.zones()) {
            const auto& object=scene_->objects[area];
            if(!actors_[area].alive)continue;
            const double radius=actors_[area].radius+player().radius;
            if(nativeDistanceSquared(p.x-object.position.x,p.y-object.position.y)>radius*radius)continue;
            // Role IDs retain ascending actor order. Read current positions,
            // liveness and radius; keep the first matching colocated terminal.
            for(const size_t i:savingRoles_.terminals())if(actors_[i].alive &&
                scene_->objects[i].position.x==object.position.x && scene_->objects[i].position.y==object.position.y){plot=i;break;}
            if(plot)break;
        }
    }
    if(plot!=savePlot_) {
        if(savePlot_&&!saveAttempted_) {
            const auto p=scene_->objects[*savePlot_].position;
            notifications_.saving(notifications_.language()==2?"Pas de sauvegarde.":"No Saving.",p.x,p.y,1000);
        }
        savePlot_=plot;saveDwellMs_=0;saveAttempted_=false;saveRequest_.reset();
        if(plot)message_.clear();
        return;
    }
    if(!plot || saveAttempted_)return;
    saveDwellMs_=std::min(5500.0,saveDwellMs_+milliseconds);
    if(saveDwellMs_<5500) {
        // 42a5af: integer (6000-elapsed)/1000; the attempt itself is at5500.
        const int remaining=static_cast<int>((6000-saveDwellMs_)/1000);
        const auto p=scene_->objects[*plot].position;
        notifications_.saving(std::string(notifications_.language()==2?"Sauvegarde dans ":"Saving in ")+std::to_string(remaining)+(notifications_.language()==2?" seconde":" second")+
                              (remaining==1?"":"s"),p.x,p.y,500);
        return;
    }
    saveAttempted_=true;
    const double reward=propertyNumber(scene_->objects[*plot].properties,"Reward");
    const int64_t cost=static_cast<int64_t>(std::clamp(-reward,0.0,static_cast<double>(std::numeric_limits<int64_t>::max()/2)));
    if(player().vitals.money<cost){
        voiceEvent("V-NEED-CASH.WAV",0,1000,5000);
        const auto p=scene_->objects[*plot].position;
        notifications_.saving(std::string(notifications_.language()==2?"Pas assez d'Argent ":"Not enough Cash ")+playerName_+(notifications_.language()==2?" !\n Il faut ":" !\n You need ")+std::to_string(cost)+
                              (notifications_.language()==2?" $\n  pour sauver cette partie ...":" $\n  to save this game ..."),p.x,p.y,5000);
        return;
    }
    saveRequest_=SaveRequest{*plot,cost};
}
void GameWorld::finishSave(bool success,const std::string& error) {
    if(!saveRequest_)return;
    const auto p=scene_->objects[saveRequest_->plot].position;
    if(success){
        actors_[player_].vitals.money-=saveRequest_->cost;
        //42a516: paid saving uses MONEY on explicit channel3. Keep the port's
        //transactional debit-on-success policy; free/failed saves stay silent.
        if(saveRequest_->cost>0)audioEvent(AudioEventKind::SaveCash,player_);
        notifications_.saving(notifications_.language()==2?"Sauvegarde Termin\xe9" "e":"Saving Completed.",p.x,p.y,3000);
    }
    else {
        notifications_.saving("Cannot save: "+error,p.x,p.y,5000);
        diagnostics_.push_back("Cannot save: "+error);
    }
    saveRequest_.reset();
}
}
