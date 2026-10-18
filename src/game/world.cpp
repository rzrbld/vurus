#include "game/world.h"
#include "game/runtime_storage.h"
#include "game/enemy_idle_native.h"
#include "game/weapon_muzzle_native.h"
#include "game/projectile_contact_native.h"
#include "game/ammo_absorption_native.h"
#include "game/ammo_radial_native.h"
#include "game/shield_regeneration_native.h"
#include "game/lift_access.h"
#include "game/ammo_contact.h"
#include "game/death_drops.h"
#include "game/event_random_native.h"
#include "game/triple_fire.h"
#include "game/properties.h"
#include "game/death_products.h"
#include "game/hud_stats.h"
#include "game/motion_contact.h"
#include "game/actor_contact_native.h"
#include "game/spider_contact_native.h"
#include "game/ammo_collision_native.h"
#include "game/enemy_aim.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace vyruz {
namespace {
// N3 scoped remaining inertia launch classes. Grenades keep their separate
// N2 contact/inheritance adapter until that investigation is completed.
bool nativeStingerFlameSawAmmo(const ObjectClass& cls) {
    for(const std::string_view name:{"stinger-ammo","flame-ammo","saw-ammo-empty",
                                    "saw-ammo","saw-ammo-vz"})
        if(contentNameIs(cls.name,name))return true;
    return false;
}
// Original Pulsar-family emitters use the same native initialization and
// authored Thrust.Max as other ammo; Speed-max is only a propulsion gate.
bool nativePulsarAmmo(const ObjectClass& cls) {
    for(const std::string_view name:{"pulsar-ammo","pulsar-ii-ammo","roller-pulsar-ammo",
                                    "turret-pulsar-ammo","helicopter-pulsar-ammo"})
        if(contentNameIs(cls.name,name))return true;
    return false;
}
// 410020/40f9b0/40fce0: direct navigation tests the centreline against
// each obstacle's own native-coordinate AABB, without the seeker's radius.
// This is used only for Spider attack admission, never physical movement.
bool nativeBoundsBlock(ContentPosition from,ContentPosition to,ContentPosition center,float radius) {
    const auto p=surfaceToNative(from.x-center.x,from.y-center.y);
    const auto q=surfaceToNative(to.x-center.x,to.y-center.y);
    float enter=0,leave=1;
    for(const auto [start,delta]:{std::pair{p.x,q.x-p.x},std::pair{p.y,q.y-p.y}}) {
        if(std::abs(delta)<1e-8f){if(start < -radius || start > radius)return false;continue;}
        float lo=(-radius-start)/delta,hi=(radius-start)/delta;if(lo>hi)std::swap(lo,hi);
        enter=std::max(enter,lo);leave=std::min(leave,hi);if(enter>leave)return false;
    }
    return true;
}
uint32_t count(float n) { return static_cast<uint32_t>(std::clamp(n,0.f,1000000.f)); }
const std::vector<TextNode>& children(const std::vector<TextNode>& nodes,const char* key) {
    static const std::vector<TextNode> empty;
    const auto* node=contentProperty(nodes,key); return node?node->children:empty;
}
const TextNode* weaponMetadata(const ObjectClass& cls) {
    // 409d00 prepends TYPE to the class list; 4375f0 reads that list directly,
    // unlike the reversed instance copy used for actor ADD Ammo inventories.
    for(auto it=cls.properties.rbegin();it!=cls.properties.rend();++it)
        if(folded(it->key)=="type" && !it->arguments.empty() && folded(it->arguments.front())=="weapon")return &*it;
    return nullptr;
}
void selectCompatibleAmmo(EquippedWeapon& weapon,const ActorState& actor,
                          const ObjectPlacement& object,const SceneContent& scene) {
    const auto* cls=findClass(scene,weapon.name);if(!cls)return;
    std::optional<size_t> emptyStock;
    // 437540 selects the first nonempty compatible ADD record, not a name sum.
    for(size_t index=0;index<actor.ammo.size();++index) {
        const auto& [name,stock]=actor.ammo.slots[index];
        for(const auto& variant:cls->properties) {
            if(folded(variant.key)!="type" || variant.arguments.empty() || folded(variant.arguments.front())!="weapon" ||
                folded(propertyText(variant.children,"Ammo"))!=name)continue;
            if(stock.count) {weapon.ammo=name;weapon.ammoSlot=index;return;}
            if(!emptyStock)emptyStock=index;
            break;
        }
    }
    weapon.ammoSlot=emptyStock;
    if(emptyStock)weapon.ammo=actor.ammo.slots[*emptyStock].first;
    (void)object;

}

}
void GameWorld::refreshSelectedAmmo() {
    auto& actor=actors_[player_];
    if(!actor.weapons.empty())selectCompatibleAmmo(actor.weapons[actor.selectedWeapon],actor,scene_->objects[player_],*scene_);
}
// 444450 -> 437540/4376a0: refresh compatible stock first, then cycle
// forward through weapon slots. No pre-Surprise weapon is saved/restored.
void GameWorld::selectLoadedPlayerWeapon() {
    auto& actor=actors_[player_];
    if(actor.weapons.empty())return;
    const auto original=actor.selectedWeapon;
    const auto loaded=[&](size_t slot) {
        auto& weapon=actor.weapons[slot];
        auto stock=weaponStock(actor,weapon);
        if(stock!=actor.ammo.end() && stock->second.count>0)return true;
        selectCompatibleAmmo(weapon,actor,scene_->objects[player_],*scene_);
        stock=weaponStock(actor,weapon);
        return stock!=actor.ammo.end() && stock->second.count>0;
    };
    if(loaded(original))return;
    for(size_t offset=1;offset<actor.weapons.size();++offset) {
        const auto slot=(original+offset)%actor.weapons.size();
        if(loaded(slot)){selectPlayerWeaponSlot(static_cast<unsigned>(slot));return;}
    }
}
void GameWorld::notifyWeapon() {
    const auto& actor=player();if(actor.weapons.empty())return;
    // 437740 selection indexes the native ADD Weapon order.
    static const char* voices[]={"V-GUN.WAV","V-GRENADE.WAV","V-ROCKETS.WAV","V-FLAME.WAV","V-SAW.WAV","V-HOMING.WAV","V-LAZER.WAV","V-PULSAR.WAV","V-MINE.WAV"};
    if(actor.selectedWeapon<std::size(voices))voiceEvent(voices[actor.selectedWeapon],750,500);
    const auto& weapon=actor.weapons[actor.selectedWeapon];
    const auto uppercase=[](std::string text){for(auto& c:text)if(c>='a'&&c<='z')c-=32;return text;};
    const auto line=[&](std::string text,float y){notifications_.greenInfo(NoticeKind::WeaponInfo,std::move(text),400,y,4000);};
    const bool french=notifications_.language()==2;
    const auto* gun=findClass(*scene_,weapon.name);
    line(std::string(french?"ARME        ":"WEAPON      ")+uppercase(gun?propertyText(gun->properties,"Name",gun->name):weapon.name),140);
    line(std::string(french?"PORTEE DE TIR  ":"FIRE-RANGE  ")+std::to_string(static_cast<int>(weapon.range)),150);
    line(std::string(french?"DELAI DE TIR  ":"FIRE-DELAY  ")+std::to_string(weapon.config.delayMs),160);
    const auto* ammo=findClass(*scene_,weapon.ammo);if(!ammo)return;
    const auto* metadata=attribute(ammo->properties,"ammo");if(!metadata)return;
    float damage=propertyNumber(metadata->children,"Damage"),radius=propertyNumber(metadata->children,"Damage-zone");
    if(damage==0 || radius==0)for(const auto& type:ammo->properties) {
        if(!contentNameIs(type.key,"type") || type.arguments.empty() ||
           (!contentNameIs(type.arguments.front(),"ammo")&&!contentNameIs(type.arguments.front(),"explosion")))continue;
        damage=std::max(damage,propertyNumber(type.children,"Damage"));
        radius=std::max(radius,propertyNumber(type.children,"Damage-zone"));
    }
    line(std::string(french?"MUNITIONS   ":"AMMO        ")+uppercase(propertyText(ammo->properties,"Name",ammo->name)),180);
    line(std::string(french?"DOMMAGE     ":"DAMAGE      ")+std::to_string(static_cast<int>(damage)),190);
    line(std::string(french?"ZONE DE DEGAT ":"DAMAGE-AREA ")+std::to_string(static_cast<int>(radius)),200);
}
void GameWorld::configurePlayerMotion() {
    const auto& p=scene_->objects[player_].properties;
    auto& c=playerMotionConfig_;c={};
    const auto& thrust=children(p,"Thrust");
    c.speedMin=propertyNumber(thrust,"Min",c.speedMin);
    c.speedMax=propertyNumber(thrust,"Max",c.speedMax);
    c.speedThreshold=propertyNumber(p,"Speed-max",c.speedThreshold);
    c.acceleration=propertyNumber(p,"Acceleration-step",c.acceleration);
    c.deceleration=propertyNumber(p,"Decceleration-step",c.deceleration);
    c.weight=propertyNumber(children(p,"Weight"),"Initial",c.weight);
    c.grip=propertyNumber(p,"Grip",c.grip);
    c.inertia=propertyBool(p,"Use-inertia",c.inertia);
    c.turnSeconds=propertyNumber(p,"Turn-timeout",18)/1000;
    const auto axis=[&](const char* name,size_t index,float fallback) {
        const auto* node=contentProperty(p,name);
        if(!node || index>=node->values.size())return fallback;
        char* end=nullptr;const float value=std::strtof(node->values[index].c_str(),&end);
        return end!=node->values[index].c_str()&&std::isfinite(value)?value:fallback;
    };
    c.frictionX=axis("Friction-factor",0,c.frictionX);
    c.frictionZ=axis("Friction-factor",1,c.frictionX);
    c.thresholdX=axis("Friction-threshold",0,c.thresholdX);
    c.thresholdZ=axis("Friction-threshold",1,c.thresholdX);
}
void GameWorld::configureWeaponSlopes(ActorState& actor) {
    constexpr float radians=.017453292f;
    for(auto& weapon:actor.weapons) {
        const auto* cls=findClass(*scene_,weapon.name);const auto* w=cls?weaponMetadata(*cls):nullptr;
        weapon.config.initialSlope=w?propertyNumber(w->children,"Initial-slope")*radians:0;
        weapon.config.slopeStep=w?propertyNumber(w->children,"Slope-step")*radians:0;
        weapon.config.oneByOne=w&&propertyBool(w->children,"One-by-one");
    }
}
void GameWorld::configureShieldRegeneration(ActorState& actor,const ObjectPlacement& object,
        const DifficultyPolicy& policy,bool player,bool legacy) {
    const auto family=objectSubtype(object);
    actor.shieldRegenerates=player || family=="cop" || family=="walker" || family=="burner" ||
        family=="jeep" || family=="roller" || family=="heli" || family=="turret" ||
        family=="spider" || family=="tank" || family=="vyruz";
    if(actor.shieldUpStep)return;
    float step=propertyNumber(object.properties,"Shield-up-step");
    if(player)step*=policy.playerShieldRegeneration;
    else if(actor.hudEnemy)step*=policy.enemyShieldRegeneration;
    // Legacy saves lack the mutable native field. Only a converted upper piece
    // is known to have been zeroed; an authored Only-base can regenerate.
    if(legacy && actor.turretOnlyBase && !propertyBool(scene_->classes[object.classId.value].properties,"Only-base") &&
       !propertyBool(object.authored.children,"Only-base"))step=0;
    actor.shieldUpStep=step;
}
void GameWorld::advanceShieldRegeneration(size_t id) {
    auto& actor=actors_[id];
    if(actor.shieldRegenerates)
        actor.vitals.shield=nativeRegenerateShield(actor.vitals.shield,actor.shieldUpStep.value_or(0),actor.vitals.shieldMax);
}
void GameWorld::configureEnemyTurning(ActorState& actor,const ObjectPlacement& object,bool legacy) {
    actor.enemyTurnConfig={};
    if(object.id.value==player_ || attribute(object.properties,"ammo"))return;
    const auto family=objectSubtype(object);int subtype=0;
    for(const auto& [name,id]:{std::pair{"cop",0x16}, {"walker",0x19}, {"burner",0x1a},
        {"jeep",0x1b},{"roller",0x1c},{"heli",0x1d},{"turret",0x1e},{"tank",0x20}})
        if(family==name){subtype=id;break;}
    if(!subtype)return; // Player/Spider controllers remain separate.
    const auto& p=scene_->classes[object.classId.value].properties;
    actor.enemyTurnConfig={subtype,propertyBool(p,"Two-pieces"),propertyBool(p,"Can-turn-on-place"),
        static_cast<int>(propertyNumber(p,"Turn-timeout")),static_cast<int>(propertyNumber(p,"Turret-turn-timeout"))};
    if(!actor.enemyTurn) {
        // Native fresh gun ring starts at0. Old saves only had one facing;
        // preserve that visible heading when migrating their missing gun state.
        actor.enemyTurn=initializeEnemyTurn(actor.enemyTurnConfig,actor.direction,legacy?actor.direction:0);
    }
    if(!actor.enemyTurn->gunStateKnown) {
        actor.enemyTurn->gunLogicalHeading=(actor.enemyTurn->gunPose-1)/enemyTurnPoseMultiplier(subtype);
        actor.enemyTurn->gunCommand=actor.enemyTurn->gunPreviousCommand=0;
        actor.enemyTurn->gunInRange=false;actor.enemyTurn->gunStateKnown=true;
    }
}
void GameWorld::configureContact(ActorState& actor,const ObjectPlacement& object) {
    const auto& p=object.properties;auto& c=actor.contactConfig;c={};
    c.weight=propertyNumber(children(p,"Weight"),"Initial",1);
    c.elasticity=propertyNumber(p,"Elasticity",1);c.grip=propertyNumber(p,"Grip",1);
    c.inertia=propertyBool(p,"Use-inertia",true);c.item=attribute(p,"item")!=nullptr;
    c.launchedThing=launchedDeathProduct(scene_->classes[object.classId.value]) &&
        propertyNumber(p,"Bounding-box-size")==0 && actor.brain.base==BrainBase::Straight;
    const auto axis=[&](const char* key,size_t index,float fallback) {
        const auto* n=contentProperty(p,key);if(!n || n->values.size()<=index)return fallback;
        char* end=nullptr;const float v=std::strtof(n->values[index].c_str(),&end);
        return end!=n->values[index].c_str() && std::isfinite(v)?v:fallback;
    };
    c.frictionX=axis("Friction-factor",0,.97f);c.frictionZ=axis("Friction-factor",1,c.frictionX);
    c.thresholdX=axis("Friction-threshold",0,.01f);c.thresholdZ=axis("Friction-threshold",1,c.thresholdX);
    c.enabled=!attribute(p,"fx") && !attribute(p,"ammo") && !attribute(p,"lift") &&
        !attribute(p,"gate") && c.weight>0 && c.elasticity>=0 && c.grip>=0 &&
        c.frictionX>=0 && c.frictionZ>=0 && c.thresholdX>=0 && c.thresholdZ>=0;
    auto& e=actor.enemyMotionConfig;e={};
    const auto& thrust=children(p,"Thrust");
    e.thrustMin=propertyNumber(thrust,"Min");e.thrustMax=propertyNumber(thrust,"Max");
    e.acceleration=propertyNumber(p,"Acceleration-step");e.deceleration=propertyNumber(p,"Decceleration-step");
    e.speedThreshold=propertyNumber(p,"Speed-max");e.weight=c.weight;e.grip=c.grip;
    e.frictionX=c.frictionX;e.frictionZ=c.frictionZ;e.thresholdX=c.thresholdX;e.thresholdZ=c.thresholdZ;
    e.inertia=c.inertia;e.launched=propertyBool(p,"Launched");
    const auto subtype=objectSubtype(object);
    // 440cf6: Ammo/StaticGen take dedicated paths before the mask excludes
    // FX/Item/Lift/Gate. Graphic-update-needed does not bypass this update gate.
    const bool unnamedLogicalBypass=subtype.empty() && (attribute(p,"ammo") || attribute(p,"fx") ||
        attribute(p,"item") || attribute(p,"lift") || attribute(p,"gate"));
    actor.nativeCameraGated=!unnamedLogicalBypass && subtype!="ammo" && subtype!="staticgen" && subtype!="fx" &&
        subtype!="item" && subtype!="lift" && subtype!="gate";
    c.nativeEnemy=subtype=="cop" || subtype=="staticgen" || subtype=="walker" || subtype=="burner" ||
        subtype=="jeep" || subtype=="roller" || subtype=="heli" || subtype=="turret" || subtype=="spider" || subtype=="tank";
    e.ignoreWeight=subtype=="walker" || subtype=="spider";
    if(!actor.enemyMotionInitialized) {
        actor.enemyThrust=propertyNumber(thrust,"Initial");
        actor.enemyMagnitude=std::hypot(actor.contactVelocity.x,actor.contactVelocity.z);
        actor.enemyMotionInitialized=true;
    }
}
ContactVelocity GameWorld::contactVelocity(size_t actor) const {
    return actor==player_?ContactVelocity{motion_.velocityX,motion_.velocityZ}:actors_[actor].contactVelocity;
}
ContactVelocity GameWorld::effectiveContactVelocity(size_t actor) const {
    //436ac0 also applies to Vyruz: a blocked commanded visit derives its
    //effective collision velocity from thrust, never the prior amplified bounce.
    if(actor==player_ && surprise_.active(SurpriseKind::Pinball)) {
        const auto p=scene_->objects[actor].position;
        if(p.x==nativePlayerPreviousSurface_.x && p.y==nativePlayerPreviousSurface_.y && playerMoveCommanded_) {
            const auto h=nativeHeading(motion_.direction);return {h.x*motion_.thrust,h.y*motion_.thrust};
        }
    }
    const auto& a=actors_[actor];
    // 436ac0: stationary commanded enemies use thrust*heading even when
    // the family-specific post-move reset left stored velocity at zero.
    if(actor!=player_ && a.enemy && !a.enemyMovedLastStep && a.enemyMoveCommand!=0) {
        const int heading=a.spiderAI && a.spiderAI->turn?a.spiderAI->turn->logicalHeading:
            objectSubtype(scene_->objects[actor])=="spider"?(a.direction/6)*6:a.direction;
        const auto h=nativeHeading(heading);
        return {h.x*a.enemyThrust,h.y*a.enemyThrust};
    }
    return contactVelocity(actor);
}
void GameWorld::setContactVelocity(size_t actor,ContactVelocity v) {
    if(actor==player_) {
        motion_.velocityX=v.x;motion_.velocityZ=v.z;
        //4367d0/436dd0 leave +148 at the propulsion result.438b20 reads
        //that cached magnitude for the NEXT acceleration decision. Preserve
        //the accepted ordinary collision adapter outside active Pinball.
        if(!surprise_.active(SurpriseKind::Pinball))motion_.magnitude=std::hypot(v.x,v.z);
    }
    else {
        auto& a=actors_[actor];a.contactVelocity=v;
        // 4406e0: ordinary non-player actors enter the knockback solver.
        a.contactMotion=!a.contactConfig.item && a.brain.base!=BrainBase::Straight;
    }
}
void GameWorld::applyAmmoImpulse(size_t actor,int heading) {
    auto& a=actors_[actor];const auto& c=a.contactConfig;
    if(!c.enabled || !a.generators.empty())return;
    const auto h=nativeHeading(heading);auto v=contactVelocity(actor);
    v.x+=h.x*40/c.weight;v.z+=h.y*40/c.weight;
    if(actor==player_){motion_.velocityX=v.x;motion_.velocityZ=v.z;motion_.magnitude=std::hypot(v.x,v.z);}
    else a.contactVelocity=v;
}
bool GameWorld::advanceContactMotion(size_t actor,float seconds) {
    auto& a=actors_[actor];const auto& c=a.contactConfig;
    if(!c.enabled || (!a.contactMotion && a.enemy && debugEnemyAI_))return false;
    const auto stationary=[&] {
        if(c.nativeEnemy){a.enemyMagnitude=0;a.enemyMovedLastStep=false;if(auto* h=a.nativeMotionHistory())h->previousSurface=scene_->objects[actor].position;}
    };
    auto v=a.contactVelocity;
    if(v.x==0 && v.z==0 && nativeActorRestDecision(actor,{}).rested){a.contactMotion=false;stationary();if(nativeActorRestDecision(actor,{}).sleep)sleepNativeActor(actor);return false;}
    if(!c.inertia && !a.contactMotion)v={};
    else if(c.inertia){v.x*=c.grip;v.z*=c.grip;}
    v.x*=c.frictionX;v.z*=c.frictionZ;
    const float scale=seconds/.014f;
    if(std::abs(v.x)<=c.thresholdX*scale && std::abs(v.z)<=c.thresholdZ*scale)v={};
    a.contactVelocity=v;
    //AI-disabled/impact-only native enemies still execute the+148 producer
    //before entering the shared contact caller; passive Items keep their adapter.
    if(c.nativeEnemy)a.enemyMagnitude=std::hypot(v.x,v.z);
    // 438b20 releases the impact latch below native speed1. Passive Items
    // retain inertia and continue coasting down to their authored threshold.
    if(a.contactMotion)a.contactMotion=std::hypot(v.x,v.z)>=1;
    if(c.nativeEnemy && v.x==0 && v.z==0 && nativeActorRestDecision(actor,{}).rested){stationary();if(nativeActorRestDecision(actor,{}).sleep)sleepNativeActor(actor);return false;}
    const auto delta=nativeToSurface(v.x*scale,v.z*scale);
    const ContactVelocity nativeStep{v.x*scale,v.z*scale};
    move(actor,delta.x,delta.y,true,&nativeStep);
    return true;
}
ActorState GameWorld::makeActor(const ObjectPlacement& object) {
    ActorState actor;
    actor.nativeSubtype=nativeObjectSubtype(object,object.id.value==player_);
    const auto& p=object.properties;
    const auto& cls=scene_->classes[object.classId.value];
    const auto& energy=children(cls.properties,"Energy"); const auto& shield=children(cls.properties,"Shield");
    actor.vitals.energy=propertyNumber(energy,"Initial",1);
    actor.vitals.energyMin=propertyNumber(energy,"Min");
    actor.vitals.energyMax=propertyNumber(energy,"Max",actor.vitals.energy);
    actor.vitals.shield=propertyNumber(shield,"Initial");
    actor.vitals.shieldMin=propertyNumber(shield,"Min");
    actor.vitals.shieldMax=propertyNumber(shield,"Max",actor.vitals.shield);
    const auto* currentEnergy=contentProperty(p,"Energy");
    const auto* currentShield=contentProperty(p,"Shield");
    if (currentEnergy) actor.vitals.energy=currentEnergy->kind==TextKind::Assignment?propertyNumber(p,"Energy",actor.vitals.energy):propertyNumber(currentEnergy->children,"Initial",actor.vitals.energy);
    if (currentShield) actor.vitals.shield=currentShield->kind==TextKind::Assignment?propertyNumber(p,"Shield",actor.vitals.shield):propertyNumber(currentShield->children,"Initial",actor.vitals.shield);
    actor.radius=std::max(0.f,propertyNumber(p,"Bounding-box-size"));
    const auto boundsSubtype=objectSubtype(object);
    if(boundsSubtype=="gate"||boundsSubtype=="lift"||
       (boundsSubtype.empty()&&(attribute(p,"gate")||attribute(p,"lift"))))
        actor.nativeGeneratorSquare=makeNativeGeneratorSquare(object.position.x,object.position.y,static_cast<int32_t>(actor.radius));
    actor.indestructible=propertyBool(p,"Indestructible");
    actor.timedProtection.reset(timedProtectionConfig(*scene_,object));
    if(actor.timedProtection.config.onMs>0&&actor.timedProtection.config.offMs>0)
        attachedProtectionActors_.push_back(object.id.value);
    actor.directFireOnly=propertyBool(p,"Player-direct-fire-only");actor.finalObject=propertyBool(p,"Final-object");
    actor.speed=std::max(0.f,propertyNumber(p,"Speed-max"));
    if(const auto* ammo=attribute(p,"ammo"))actor.contactMine=actor.speed==0 && propertyNumber(ammo->children,"damage")==0;
    actor.lifeMs=propertyNumber(cls.properties,"Life-Time",-1);
    actor.nativeManaged=nativeClassManaged(cls.properties);
    if (actor.lifeMs<=0) actor.lifeMs=-1;
    actor.ageMs=std::max(0.f,propertyNumber(object.authored.children,"Life-Time"));
    actor.brain=makeBrainConfig(object,*scene_);
    actor.enemy=actor.brain.seeker || actor.brain.tracker || actor.brain.base==BrainBase::Patrol;
    actor.directional=actor.enemy;actor.hudEnemy=countsAsEnemy(object,*scene_);
    actor.turretOnlyBase=objectSubtype(object)=="turret" && propertyBool(p,"Only-base");
    if(actor.turretOnlyBase) {
        actor.brain.base=BrainBase::Fixed;actor.brain.seeker=actor.brain.tracker=false;
        actor.hudEnemy=false; // 4371b0: Fixed without targeting is no enemy.
    }
    if (const auto* item=attribute(p,"item")) {
        actor.pickup=propertyBool(item->children,"Only-for-player");
        actor.pickupEffect={propertyNumber(item->children,"Add-NRJ"),
                            propertyNumber(item->children,"Add-Shield"),
                            static_cast<int64_t>(propertyNumber(item->children,"Add-Money"))};
    }
    const bool savingZone=propertyBool(p,"Saving-zone");
    if(savingZone || propertyBool(p,"Saving-plot"))actor.pickup=false;
    // Native 4406e0 bypasses contact response for Saving-zone only. The
    // colocated visible Saving-plot remains an ordinary solid Item (radius30).
    actor.solid=actor.radius>0 && !actor.pickup && !attribute(p,"lift") && !attribute(p,"ammo") &&
        !savingZone && (!object.flags.invisible.value_or(false) || propertyBool(p,"Keep-bb-when-invisible"));
    actor.lift=makeLiftConfig(object);
    if(actor.lift) {
        actor.liftState=initializeLift(*actor.lift);actor.gateRevision=1;
        actor.solid=!actor.liftState.passable;actor.enemy=false;actor.directional=false;
    }
    for (const auto& node:p) {
        if (folded(node.key)!="add" || node.arguments.empty()) continue;
        const auto name=propertyText(node.children,"Name");
        if (folded(node.arguments.front())=="ammo")
            actor.ammo.append(folded(name),{count(propertyNumber(node.children,"Count")),count(propertyNumber(node.children,"Max"))});
        if (folded(node.arguments.front())!="weapon") continue;
        const auto* cls=findClass(*scene_,name);
        const auto* weapon=cls?weaponMetadata(*cls):nullptr;
        if (!weapon) { diagnostics_.push_back("unresolved weapon: "+name); continue; }
        const auto& w=weapon->children;
        actor.weapons.push_back({name,folded(propertyText(w,"Ammo")),
            {count(propertyNumber(w,"Delay")),propertyNumber(w,"Weapons-gap"),
             propertyBool(w,"Left-side"),propertyBool(w,"Right-side"),propertyBool(w,"Alternated-fire")},
            {},propertyNumber(w,"Fire-Range"),propertyBool(w,"Visible-gun-fire"),{}});
    }
    for(auto& weapon:actor.weapons)selectCompatibleAmmo(weapon,actor,object,*scene_);
    // Original 0043c4a0 applies difficulty after actor/class construction.
    // Easy increases maximum capacity, retaining authored initial health/shield.
    if(object.id.value==player_) {
        if(difficulty_.playerEnergyMax>0)actor.vitals.energyMax=difficulty_.playerEnergyMax;
        if(difficulty_.playerShieldMax>0)actor.vitals.shieldMax=difficulty_.playerShieldMax;
        // 43c4a0 clamps class defaults, not current placement values. 413d80 /
        // 41a210 preserve explicit Energy/Shield assignments (DEMO uses20000).
        if(!currentEnergy || currentEnergy->kind!=TextKind::Assignment)
            actor.vitals.energy=std::min(actor.vitals.energy,actor.vitals.energyMax);
        if(!currentShield || currentShield->kind!=TextKind::Assignment)
            actor.vitals.shield=std::min(actor.vitals.shield,actor.vitals.shieldMax);
    } else {
        if(actor.enemy) {
            actor.brain.scanDelayMs*=difficulty_.enemyScanDelay;
            actor.brain.sensorRange*=difficulty_.enemyScanRange;
        }
        for(auto& weapon:actor.weapons)weapon.config.delayMs=count(weapon.config.delayMs*difficulty_.enemyFireDelay);
    }
    for (size_t i=0;i<actor.weapons.size();++i) {
        auto stock=weaponStock(actor,actor.weapons[i]);
        if (stock!=actor.ammo.end() && stock->second.count) { actor.selectedWeapon=i; break; }
    }
    configureWeaponSlopes(actor);
    configureContact(actor,object);
    configureEnemyTurning(actor,object);
    configureShieldRegeneration(actor,object,difficulty_,object.id.value==player_);
    configureOrdinaryEnemyAI(actor,object);
    configureGenerators(actor,object);
    configureActorCreates(actor,object);
    return actor;
}
bool GameWorld::load(SceneContent& scene,const Level& level,std::string& error,int difficulty) {
    DifficultyPolicy policy;
    if(!loadDifficultyPolicy(scene,difficulty,policy,error))return false;
    if (!scene.player || scene.player->value>=scene.objects.size()) { error="gameplay requires an authored player";return false; }
    for (const auto& object:scene.objects) if (object.classId.value>=scene.classes.size()) {
        error="invalid runtime class reference";return false;
    }
    retaliationTarget_=SIZE_MAX;retaliationGeneration_=0;
    difficulty_=policy;debugEnemyAI_=true;debugGodMode_=false;playerLocked_=false;
    spiderNavigation_.reset();spiderNavigationActors_.clear();
    scene_=&scene;level_=&level;player_=scene.player->value;elapsedMs_=0;playerKills_=0;ammoHits_=ammoMisses_=0;motion_={};map_={};
    catalogHash_=contentFingerprint();savePlot_.reset();saveDwellMs_=0;saveAttempted_=false;saveRequest_.reset();
    effectSequences_.clear();attachedProtectionActors_.clear();actors_.clear();projectiles_.clear();transition_.reset();rejectedLifts_.clear();diagnostics_.clear();message_.clear();
    damageQueue_.clear();contactDamage_.clear();applyingDamage_=false;surprise_={};touchVictims_.clear();heldItems_.clear();
    actorCells_={};bareAmmoCells_={};bareAmmoIndex_.clear();visualAmmoIndex_.clear();
    awakeManager_.entries.clear();awakeVisitIndex_.reset();deferActorFire_=false;deferredActorFire_.reset();
    resetNativePoolForLoad();nextNativeCellOrder_=nextNativeBodyIdentity_=1;
    diagnosedActorCreates_.clear();actorCreateDepth_=0;
    shotsFired=pickupsTaken=actorsDestroyed=0;audioEvents_.clear();audioInput_.reset();audioMotorAttempt_=false;playerMoveCommanded_=false;notifications_.clear();
    contactMines_.clear();ammoContactVictims_.clear();
    const size_t placed=scene.objects.size();
    reserveRuntimeStorage(actors_);reserveRuntimeStorage(scene.objects);
    reserveRuntimeStorage(actorBounds_);reserveRuntimeStorage(actorBoundGroup_);reserveRuntimeStorage(actorBoundOrder_);
    reserveRuntimeStorage(actorBoundGroups_,(RuntimeActorLimit+15)/16);
    actors_.resize(placed);
    actorCells_.reset(std::max(0,level.mapW),std::max(0,level.mapH));
    reserveRuntimeStorage(actorCells_.cell);reserveRuntimeStorage(actorCells_.next);reserveRuntimeStorage(actorCells_.arrivalOrder);
    bareAmmoCells_.reset(actorCells_.width,actorCells_.height);
    random_.seed(0x56595255);nativeWorldRandom_={};nativeGlobalRandom_={};nativeReplayRandom_={};
    constructingScene_=true;
    //4159b2 finishes each placement (and its recursive CREATE) before the next
    //text record. Stable port IDs need not equal native allocation order.
    for(size_t id=0;id<placed;++id) {
        actors_[id]=makeActor(scene.objects[id]);
        initializeNativeIdentity(id);
        registerSavingPrice(id);
        if(actors_[id].contactMine)contactMines_.push_back(id);
        updateActorCreates(id,scene.objects[id].position,true);
        synchronizeActorCell(id); //415b5a, after all construction children.
    }
    constructingScene_=false;
    for(size_t id=0;id<actors_.size();++id) {
        const auto owner=scene_->objects[id].owner;
        assignNativeCreator(id,owner?std::optional<size_t>{owner->value}:std::nullopt);
    }
    motion_.thrust=propertyNumber(children(scene.objects[player_].properties,"Thrust"),"Initial");
    configurePlayerMotion();
    nativePlayerFacing_=initializeNativePlayerFacing(motion_.direction,std::max(1,int(playerMotionConfig_.turnSeconds*1000)));
    nativePlayerPreviousSurface_=scene.objects[player_].position;
    initializeGates();
    navigationCursor_=0;freeTrailSlots_.clear();
    fastKillMs_=0;fastKillCount_=0;
    endingSource_.reset();endingRemainingMs_=0;
    // 43c700 counts all linked hostile actors, including temporarily inactive ones.
    bonusEnemies_=std::count_if(actors_.begin(),actors_.end(),[](const auto& a){return a.hudEnemy;});
    std::set<std::string> unsupported;
    for (size_t i=0;i<actors_.size();++i) if (actors_[i].enemy && actors_[i].brain.base==BrainBase::Random && !actors_[i].ordinaryAIConfig.enabled)
        unsupported.insert(scene.classes[scene.objects[i].classId.value].name);
    for (const auto& name:unsupported) diagnostics_.push_back("random wandering uses port route/occupancy approximation: "+name);
    initializeNativeTileBodies();
    initializeSpiderNavigation();
    return true;
}
void GameWorld::setDebugEnemyAI(bool enabled) {
    debugEnemyAI_=enabled;
    if(!enabled)for(size_t i=0;i<actors_.size();++i)if(actors_[i].enemy) {
        actors_[i].locomoting=false;actors_[i].enemyMoveCommand=0;actors_[i].nativeFireRequested=false;
        for(auto& weapon:actors_[i].weapons)weapon.state={};
        audioEvent(AudioEventKind::WeaponStop,i);
    }
}
void GameWorld::debugFillAmmo() {
    auto& player=actors_[player_];
    // Debug-only: restore authored class capacities for absent or disabled
    // stocks (T2U1 explicitly sets several placement maxima to zero).
    // Positive instance capacities and duplicate stock order remain intact.
    const auto& defaults=scene_->classes[scene_->objects[player_].classId.value].properties;
    const auto fill=[&](auto& stocks) {
        for(const auto& node:defaults) {
            if(folded(node.key)!="add" || node.arguments.empty() || folded(node.arguments.front())!="ammo")continue;
            const auto name=folded(propertyText(node.children,"Name"));
            const auto maximum=count(propertyNumber(node.children,"Max"));
            const bool compatible=std::any_of(player.weapons.begin(),player.weapons.end(),[&](const auto& weapon){
                const auto* cls=findClass(*scene_,weapon.name);if(!cls)return false;
                for(const auto& variant:cls->properties)
                    if(folded(variant.key)=="type" && !variant.arguments.empty() && folded(variant.arguments.front())=="weapon" &&
                       folded(propertyText(variant.children,"Ammo"))==name)return true;
                return false;
            });
            if(!compatible)continue;
            if(!stocks.contains(name))stocks[name]={0,maximum};
            else for(auto& [stockName,stock]:stocks)if(stockName==name && !stock.maximum)stock.maximum=maximum;
        }
        for(auto& [name,stock]:stocks)stock.count=stock.maximum;
    };
    fill(player.ammo);
    if(surprise_.active(SurpriseKind::Ammo)) {
        fill(surprise_.savedAmmo);
        for(const auto& [name,stock]:player.ammo)if(!surprise_.savedAmmo.contains(name))surprise_.savedAmmo.append(name,stock);
    }
    for(auto& weapon:player.weapons)selectCompatibleAmmo(weapon,player,scene_->objects[player_],*scene_);
}
bool GameWorld::debugAddMoney() {
    auto& money=actors_[player_].vitals.money;
    constexpr int64_t amount=1000000;
    if(money>std::numeric_limits<int64_t>::max()-amount)return false;
    money+=amount;return true;
}
void GameWorld::setPlayerPosition(float x,float y) { scene_->objects[player_].position={x,y}; motion_.velocityX=motion_.velocityZ=motion_.magnitude=motion_.thrust=0;actors_[player_].nativeAmmoContinuation=false;nativePlayerFacing_=initializeNativePlayerFacing(motion_.direction);nativePlayerPreviousSurface_={x,y};if(auto* h=actors_[player_].nativeMotionHistory()){const auto p=spiderSurfaceToNative(x,y);*h={{p.x,p.y},{x,y}};}synchronizeActorCell(player_); }
void GameWorld::setPlayerDirection(int direction) { motion_.direction=(direction%24+24)%24;actors_[player_].direction=motion_.direction;nativePlayerFacing_=initializeNativePlayerFacing(motion_.direction); }
bool GameWorld::selectPlayerWeaponSlot(unsigned slot) {
    auto& actor=actors_[player_];
    if(slot>=actor.weapons.size())return false;
    if(slot==actor.selectedWeapon)return true;
    actor.selectedWeapon=slot;
    actor.nativeAmmoOwner.activeAmmo=0; //43bade, selection resets actor+12c.
    refreshSelectedAmmo();actor.weapons[slot].state.lastFireMs.reset();
    audioEvent(AudioEventKind::WeaponStop,player_);audioEvent(AudioEventKind::WeaponSwitch,player_);
    notifyWeapon();return true;
}
bool GameWorld::clearRoute(ContentPosition from,ContentPosition to,float radius) const {
    return terrainMoveAllowed(*level_,from.x,from.y,to.x,to.y,radius);
}
void GameWorld::updateActorBounds(size_t actor) {
    //A factory append may move later in this same live pass. Its first bounds
    //update must grow the acceleration cache before writing the newborn slot.
    if(actorBounds_.size()<actors_.size())actorBounds_.resize(actors_.size());
    if(actorBoundGroup_.size()<actors_.size())actorBoundGroup_.resize(actors_.size(),std::numeric_limits<size_t>::max());
    updateSpiderNavigation(actor);
    const auto p=scene_->objects[actor].position;
    // Slightly oversized native-to-surface radii keep this rejection conservative.
    const double rx=double(actors_[actor].radius)*1.266, ry=double(actors_[actor].radius)*.633;
    const ActorBounds b{p.x-rx,p.y-ry,p.x+rx,p.y+ry};
    actorBounds_[actor]=b;
    if(actorBoundsActive_ && actorBoundGroup_[actor]!=std::numeric_limits<size_t>::max()) {
        // Expand after sequential actor movement; never hide a later collision.
        auto& group=actorBoundGroups_[actorBoundGroup_[actor]];
        group.minX=std::min(group.minX,b.minX);group.minY=std::min(group.minY,b.minY);
        group.maxX=std::max(group.maxX,b.maxX);group.maxY=std::max(group.maxY,b.maxY);
    }
}
bool GameWorld::dynamicBlocked(size_t actor,ContentPosition from,ContentPosition to,float radius,bool ignorePlayer,bool nativeBounds) const {
    const double rx=double(radius)*1.266, ry=double(radius)*.633;
    const double minX=std::min(from.x,to.x)-rx, maxX=std::max(from.x,to.x)+rx;
    const double minY=std::min(from.y,to.y)-ry, maxY=std::max(from.y,to.y)+ry;
    const auto outside=[&](const ActorBounds& b) {
        // Stored groups enclose projected circles. Expanding by sqrt(2)
        // conservatively encloses native squares too, including grouped bounds.
        const double ex=nativeBounds?(b.maxX-b.minX)*.208:0;
        const double ey=nativeBounds?(b.maxY-b.minY)*.208:0;
        return maxX<b.minX-ex || minX>b.maxX+ex || maxY<b.minY-ey || minY>b.maxY+ey;
    };
    const auto blocks=[&](size_t i) {
        if (i==actor || (ignorePlayer && i==player_) || !actors_[i].alive || !actors_[i].solid) return false;
        const auto q=scene_->objects[i].position;
        if(nativeBounds)return nativeBoundsBlock(from,to,q,actors_[i].radius);
        return sweptCircleContact(from.x,from.y,to.x,to.y,q.x,q.y,radius+actors_[i].radius).has_value();
    };
    size_t unindexed=0;
    if(actorBoundsActive_) {
        // Spatially grouped compact bounds keep navigation queries away from
        // distant actors' large state/property records. Exact tests are unchanged.
        for(size_t g=0;g<actorBoundGroups_.size();++g) {
            if(outside(actorBoundGroups_[g]))continue;
            const size_t end=std::min((g+1)*16,actorBoundOrder_.size());
            for(size_t n=g*16;n<end;++n) {
                const auto i=actorBoundOrder_[n];
                if(!outside(actorBounds_[i]) && blocks(i))return true;
            }
        }
        unindexed=actorBoundsIndexedCount_;
    }
    // Newly spawned actors, or calls outside a tick, use the original scan.
    for(size_t i=unindexed;i<actors_.size();++i)if(blocks(i))return true;
    return false;
}
void GameWorld::notifyAccessBlocked(size_t id) {
    // 4406e0 bypasses ordinary actor contacts for an invisible player before
    // its Gate/Lift notice branches. Do not surface unreachable warning strings.
    if(scene_->objects[player_].flags.invisible.value_or(false))return;
    const auto& actor=actors_[id];if(!actor.solid || (!actor.gate&&!actor.lift))return;
    const int key=actor.lift?actor.liftState.keyId:actor.gateState.keyId;
    const bool code=actor.lift?bool(actor.liftState.flags&LiftNeedsCode):bool(actor.gateState.flags&GateNeedsCode);
    std::string text;float offset=200;
    if(key && !hasKey(player().heldKeys,key) && !player().keys.contains(key)) {
        if(actor.gate)voiceEvent(code?"V-NEED-CODE.WAV":"V-NEED-KEY.WAV",0,1000,5000);
        const bool french=notifications_.language()==2;
        if(french) {
            if(actor.lift)text=code?"L'ascenseur requiert le code complet !":"L'ascenseur requiert la clef #"+std::to_string(key)+" !";
            else text=std::string(code?"La porte requiert le code #":"La porte requiert la clef #")+std::to_string(key)+(code?" complet !":" !");
        } else if(actor.lift)text=code?"Lift needs full code !":"Lift needs key #"+std::to_string(key)+" !";
        else text=std::string(code?"Gate needs full code #":"Gate needs key #")+std::to_string(key)+" !";
    } else if(actor.lift){text=notifications_.language()==2?"Patientez, l'ascenseur arrive.":"Wait, lift will come.";offset=150;}
    else return;
    const auto p=scene_->objects[id].position;
    notifications_.access(NoticeKind::AccessBlocked,std::move(text),std::trunc(p.x)-offset,std::trunc(p.y)-32,true);
}
TerrainDisplacement GameWorld::move(size_t actor,float dx,float dy,bool physical,const ContactVelocity* nativeStep) {
    lastSpiderSeparated_=false;
    auto& p=scene_->objects[actor].position;const auto from=p;
    const auto radius=actors_[actor].radius;
    TerrainContacts terrain;
    std::optional<ContentPosition> pinballBouncePoint;
    ContentPosition pinballBounceSource{};
    const bool nativeSpider=physical && objectSubtype(scene_->objects[actor])=="spider";
    const bool nativePlayerPinball=actor==player_ && surprise_.active(SurpriseKind::Pinball);
    // Keep the accepted continuous player wall sweep. Native endpoint body
    // queries can miss a thin wall entirely under amplified Pinball momentum.
    const bool nativeBody=physical && actor!=player_ && actors_[actor].contactConfig.nativeEnemy;
    const bool continuing=physical && actors_[actor].nativeAmmoContinuation;
    const auto convertedStep=surfaceToNative(dx,dy);
    const ContactVelocity rawStep=nativeStep?*nativeStep:ContactVelocity{convertedStep.x,convertedStep.y};
    //44169a rest does not commit divergent bounds left by an impact-abort.
    if(continuing && nativeActorRestDecision(actor,rawStep).rested){
        if(auto* h=actors_[actor].nativeMotionHistory())h->previousSurface=from;
        if(actor==player_)motion_.velocityX=motion_.velocityZ=0;
        else actors_[actor].contactVelocity={};
        return {};
    }
    if(nativeBody && !actors_[actor].nativeMotionHistory()) {
        const auto n=spiderSurfaceToNative(p.x,p.y);
        actors_[actor].nativeMotion=SpiderMotionHistory{{n.x,n.y},p};
    }
    auto* history=nativeBody || continuing?actors_[actor].nativeMotionHistory():nullptr;
    Displacement source{},proposed{},proposedSurface{};
    if(history) {
        source={history->center.x,history->center.y};
        const auto step=Displacement{rawStep.x,rawStep.z};
        proposedSurface=spiderNativeToSurface(source.x+step.x,source.y+step.y);
        proposed=spiderSurfaceToNative(proposedSurface.x,proposedSurface.y);
        dx=proposedSurface.x-p.x;dy=proposedSurface.y-p.y;
        if(continuing){
            //44181e/441992 reenters old then proposed cells, even if the final
            //cell equals the previous abort's cell. This preserves head order.
            const auto oldSurface=spiderNativeToSurface(source.x,source.y);
            p={oldSurface.x,oldSurface.y};synchronizeActorCell(actor);
            p=from;
        }
    }
    const auto relativeNativeCenter=[&](size_t target) {
        const auto q=scene_->objects[target].position;
        if(!history)return surfaceToNative(q.x-p.x,q.y-p.y);
        auto n=spiderSurfaceToNative(q.x,q.y);
        if(const auto* h=actors_[target].nativeMotionHistory())n={h->center.x,h->center.y};
        return Displacement{n.x-source.x,n.y-source.y};
    };
    auto delta=nativeBody?TerrainDisplacement{dx,dy,false}:
        moveOnTerrain(*level_,p.x,p.y,dx,dy,radius,physical?&terrain:nullptr,!physical);
    float t=1;std::optional<size_t> contact;
    std::optional<ContactVelocity> nativeContactPoint;
    std::optional<ContactVelocity> nativeTerrainNormal;
    std::optional<std::vector<uint64_t>> nativeContactOrder;
    size_t nativeContactRank=0;
    NativeBodyContactQuery sharedContact;
    const auto oldBody=history?ContactVelocity{source.x,source.y}:
        ContactVelocity{spiderSurfaceToNative(p.x,p.y).x,spiderSurfaceToNative(p.x,p.y).y};
    const auto nativeTrialSurface=spiderNativeToSurface(
        float(static_cast<long double>(oldBody.x)+rawStep.x),
        float(static_cast<long double>(oldBody.z)+rawStep.z));
    const auto nativeTrial=spiderSurfaceToNative(nativeTrialSurface.x,nativeTrialSurface.y);
    const auto nextBody=history?ContactVelocity{proposed.x,proposed.y}:ContactVelocity{nativeTrial.x,nativeTrial.y};
    if(continuing){
        auto& a=actors_[actor];
        const uint32_t behavior=(actor==player_?3u:a.brain.tracker?2u:a.brain.seeker?1u:0u)|
            (a.brain.base==BrainBase::Patrol?0xcu:0u);
        const float factor=currentTickMilliseconds_>0?14.f/static_cast<float>(currentTickMilliseconds_):1.f;
        const auto velocity=nativeActorAmmoAbortVelocity(nativeDamageSubtype(actor),behavior,oldBody,nextBody,rawStep,factor);
        if(actor==player_){motion_.velocityX=velocity.x;motion_.velocityZ=velocity.z;}
        else a.contactVelocity=velocity;
        const auto previous=spiderNativeToSurface(oldBody.x,oldBody.z);
        history->previousSurface={previous.x,previous.y};
        p={proposedSurface.x,proposedSurface.y};synchronizeActorCell(actor);
    }
    if(physical && (nativeBody || actor==player_ || actors_[actor].contactConfig.enabled))
        sharedContact=queryNativeBodyContacts(actor,oldBody,nextBody,radius);
    // Accepted player/passive movement stays unchanged when the local common
    //query has no Ammo contact. When Ammo competes, consume the single winner
    //for walls/actors/Items too; a second proximity sweep would bypass priority.
    const bool shared=sharedContact.used && (nativeBody || continuing || sharedContact.hasAmmoCandidate);
    if(shared) {
        delta={dx,dy,false};
        if(sharedContact.selected && sharedContact.selectedShape && !sharedContact.callerIgnored) {
            const auto& shape=*sharedContact.selectedShape;
            if(shape.filter.kind==3 && shape.filter.subtype==0x10) {
                if(!actors_[actor].nativeMotionHistory())actors_[actor].nativeMotion=SpiderMotionHistory{{oldBody.x,oldBody.z},from};
                p=history?ContentPosition{proposedSurface.x,proposedSurface.y}:
                    ContentPosition{nativeTrialSurface.x,nativeTrialSurface.y};synchronizeActorCell(actor);
                if(applyNativeActorAmmoContact(actor,sharedContact,
                        static_cast<float>(currentTickMilliseconds_),oldBody,nextBody,rawStep,
                        currentTickMilliseconds_>0?14.f/static_cast<float>(currentTickMilliseconds_):1.f)) {
                    actors_[actor].nativeAmmoAbort=true;
                    updateSpiderNavigation(actor);
                    return {scene_->objects[actor].position.x-from.x,scene_->objects[actor].position.y-from.y,false,true};
                }
                p=from;synchronizeActorCell(actor);
                if(shape.filter.ignoreOwner && nativeAmmoOwnedBy(shape.filter,nativeBodyContactObject(actor)))
                    sharedContact.callerIgnored=true;
            }
            if(!sharedContact.callerIgnored) {
                nativeContactPoint=ContactVelocity{sharedContact.selected->point.x-oldBody.x,
                                                  sharedContact.selected->point.z-oldBody.z};
                // Retain the original IEEE winner. Physical actor response
                //cannot publish NaN momentum into a portable checkpoint; keep
                //the established finite proposed-point response at this boundary.
                if(!std::isfinite(nativeContactPoint->x)||!std::isfinite(nativeContactPoint->z))
                    nativeContactPoint=ContactVelocity{nextBody.x-oldBody.x,nextBody.z-oldBody.z};
                t=0;
                if(shape.filter.kind==3 && sharedContact.selected->reference<actors_.size())
                    contact=static_cast<size_t>(sharedContact.selected->reference);
                else if(shape.filter.kind==1 || shape.filter.kind==2) {
                    const ContactVelocity lo{shape.minimum.x-oldBody.x,shape.minimum.z-oldBody.z};
                    const ContactVelocity hi{shape.maximum.x-oldBody.x,shape.maximum.z-oldBody.z};
                    nativeTerrainNormal=shape.filter.shape==4?spiderRectangleNormal(lo,hi,*nativeContactPoint,radius):
                        lo.x==hi.x?ContactVelocity{lo.x>=0?radius:-radius,0}:ContactVelocity{0,lo.z>=0?radius:-radius};
                }
            }
        }
    }
    if(!shared && nativeBody && spiderNavigation_ && actor<spiderNavigationActors_.size() &&
       spiderNavigationActors_[actor]<spiderNavigation_->objects.size()) {
        const auto goal=history?proposed:surfaceToNative(p.x+dx,p.y+dy);
        nativeContactOrder=spiderPhysicalContactOrder(*spiderNavigation_,spiderNavigationActors_[actor],{goal.x,goal.y},radius);
    }
    const auto physicalRank=[&](uint64_t reference)->std::optional<size_t> {
        if(!nativeContactOrder)return static_cast<size_t>(reference);
        for(size_t n=nativeContactOrder->size();n>0;--n)if((*nativeContactOrder)[n-1]==reference)return n;
        return std::nullopt;
    };
    const auto consider=[&](size_t i) {
        if(i==actor || !actors_[i].alive || !actors_[i].solid ||
           (actor==player_ && surprise_.active(SurpriseKind::Drone)))return;
        const auto q=scene_->objects[i].position;
        // 41a5d0: ordinary actors use circles; Lift/Gate use squares.
        // Size<2 Ammo/Thing may instead be points.
        const bool nativeShape=nativeBody && actors_[i].radius>=2;
        if(nativeShape) {
            const auto rank=physicalRank(i);if(!rank)return;
            const auto step=history?Displacement{proposed.x-source.x,proposed.y-source.y}:surfaceToNative(delta.x,delta.y);
            const auto target=relativeNativeCenter(i);
            const auto point=actors_[i].gate || actors_[i].lift?
                spiderSquareContact({}, {step.x,step.y},radius,{target.x,target.y},actors_[i].radius):
                spiderCircleContact({step.x,step.y},radius,{target.x,target.y},actors_[i].radius);
            if(!point)return;
            if(i==player_ && surprise_.active(SurpriseKind::Touch) && !actors_[actor].indestructible) {
                actors_[actor].vitals.energy=0;actors_[actor].solid=false;touchVictims_.push_back(actor);
            }
            // 4449f0 restores the source before 436a50 tests the contact point.
            // 4105d0/41b770 chooses the contact closest to proposed center;
            // response/admission happens only after that selection.
            const float distance=(point->x-step.x)*(point->x-step.x)+(point->z-step.y)*(point->z-step.y);
            const float previous=nativeContactPoint?
                (nativeContactPoint->x-step.x)*(nativeContactPoint->x-step.x)+
                (nativeContactPoint->z-step.y)*(nativeContactPoint->z-step.y):std::numeric_limits<float>::infinity();
            if(!contact || distance<previous || (distance==previous && *rank>=nativeContactRank))
                {t=0;contact=i;nativeContactPoint=point;nativeContactRank=*rank;}
            return;
        }
        if(physical && actors_[actor].enemy) {
            const auto travel=surfaceToNative(delta.x,delta.y),normal=surfaceToNative(q.x-p.x,q.y-p.y);
            // 436dd0 return2 commits separating motion instead of blocking it.
            if(travel.x*normal.x+travel.y*normal.y<0)return;
        }
        const auto hit=sweptCircleContact(p.x,p.y,p.x+delta.x,p.y+delta.y,q.x,q.y,radius+actors_[i].radius);
        if(hit && actor==player_ && surprise_.active(SurpriseKind::Touch) && !actors_[i].indestructible) {
            actors_[i].vitals.energy=0;actors_[i].solid=false;touchVictims_.push_back(i);return;
        }
        if(hit && i==player_ && surprise_.active(SurpriseKind::Touch) && !actors_[actor].indestructible) {
            actors_[actor].vitals.energy=0;actors_[actor].solid=false;touchVictims_.push_back(actor);
        }
        if(hit && (*hit<t || (*hit==t && (!contact || i<*contact)))){t=*hit;contact=i;nativeContactPoint.reset();}
    };
    // Reuse the crowded-level spatial bounds; moving passive Items must not
    // turn every active body into an all-actors collision scan.
    if(!shared) {
    if(actorBoundsActive_) {
        const double broadRadius=radius+(nativeBody?2.f:0.f);
        const double rx=broadRadius*1.266,ry=broadRadius*.633;
        const double x0=std::min(p.x,p.x+delta.x)-rx,x1=std::max(p.x,p.x+delta.x)+rx;
        const double y0=std::min(p.y,p.y+delta.y)-ry,y1=std::max(p.y,p.y+delta.y)+ry;
        const auto outside=[&](const ActorBounds& b){return x1<b.minX || x0>b.maxX || y1<b.minY || y0>b.maxY;};
        for(size_t g=0;g<actorBoundGroups_.size();++g)if(!outside(actorBoundGroups_[g]))
            for(size_t n=g*16;n<std::min((g+1)*16,actorBoundOrder_.size());++n) {
                const auto i=actorBoundOrder_[n];if(!outside(actorBounds_[i]))consider(i);
            }
        for(size_t i=actorBoundsIndexedCount_;i<actors_.size();++i)consider(i);
    } else for(size_t i=0;i<actors_.size();++i)consider(i);
    if(nativeBody) {
        const auto step=history?Displacement{proposed.x-source.x,proposed.y-source.y}:surfaceToNative(dx,dy);
        const double rx=double(radius+2)*1.266,ry=double(radius+2)*.633;
        const double x0=std::min(p.x,p.x+dx)-rx,x1=std::max(p.x,p.x+dx)+rx;
        const double y0=std::min(p.y,p.y+dy)-ry,y1=std::max(p.y,p.y+dy)+ry;
        const auto edgeContact=[&](size_t index) {
            const auto& edge=level_->collisionEdges[index];
            // 4406e0 skips native water-kind2 contacts for flag80000.
            if(edge.waterBoundary && propertyBool(scene_->objects[actor].properties,"Over-water"))return;
            auto aa=surfaceToNative(edge.x0-p.x,edge.y0-p.y),bb=surfaceToNative(edge.x1-p.x,edge.y1-p.y);
            ContactVelocity a{aa.x,aa.y},b{bb.x,bb.y},point{};
            // Projection roundoff must not turn native axis lines diagonal.
            if(std::abs(a.x-b.x)<std::abs(a.z-b.z))b.x=a.x;else b.z=a.z;
            if(!spiderCircleEdgeEndpoint({step.x,step.y},radius,a,b,point) &&
               !spiderCircleEdgeCrossing({}, {step.x,step.y},radius,a,b,point))return;
            const auto distance=[&](ContactVelocity q){const float x=q.x-step.x,z=q.z-step.y;return x*x+z*z;};
            if(nativeContactPoint && distance(*nativeContactPoint)<distance(point))return;
            nativeContactPoint=point;contact.reset();t=0;
            // 436b60 uses the rolled-back source bounds on an axis line.
            nativeTerrainNormal=a.x==b.x?ContactVelocity{a.x>=0?radius:-radius,0}:
                ContactVelocity{0,a.z>=0?radius:-radius};
        };
        if(!level_->nativeNavigationDecor.empty()) {
            const auto origin=history?source:surfaceToNative(p.x,p.y);
            const auto decorContact=[&](size_t index) {
                const auto rank=physicalRank((uint64_t(1)<<63)|index);if(!rank)return;
                const auto& shape=level_->nativeNavigationDecor[index];
                if(shape.disabled || (shape.water && propertyBool(scene_->objects[actor].properties,"Over-water")))return;
                const ContactVelocity lo{shape.minX-origin.x,shape.minY-origin.y},hi{shape.maxX-origin.x,shape.maxY-origin.y};
                const float lowX=std::min(0.f,step.x)-radius,highX=std::max(0.f,step.x)+radius;
                const float lowZ=std::min(0.f,step.y)-radius,highZ=std::max(0.f,step.y)+radius;
                if(hi.x<lowX || highX<lo.x || hi.z<lowZ || highZ<lo.z)return;
                std::optional<ContactVelocity> point;
                ContactVelocity result{};
                if(shape.rectangle && actors_[actor].enemyTurnConfig.subtype==0x16) {
                    // 41a470/4069d0 consumes absolute native bounds. Moving the
                    // rectangle into a source-relative frame changes the
                    // descending-edge scratch coordinate and can admit a Cop
                    // into a building corner. Keep the established finite,
                    // zero-initialized scratch boundary; the PE leaves that
                    // scratch uninitialized. Translate only the selected point
                    // for the existing response/admission adapter below.
                    point=nativeAmmoRectanglePoint({origin.x,origin.y},{proposed.x,proposed.y},radius,
                        {shape.minX,shape.minY},{shape.maxX,shape.maxY});
                    if(point){point->x-=origin.x;point->z-=origin.y;}
                } else if(shape.rectangle)point=spiderRectangleContact({}, {step.x,step.y},radius,lo,hi);
                else if(spiderCircleEdgeEndpoint({step.x,step.y},radius,lo,hi,result) ||
                        spiderCircleEdgeCrossing({}, {step.x,step.y},radius,lo,hi,result))point=result;
                if(!point)return;
                const auto distance=[&](ContactVelocity q){const float x=q.x-step.x,z=q.z-step.y;return x*x+z*z;};
                if(nativeContactPoint && (distance(*nativeContactPoint)<distance(*point) ||
                   (distance(*nativeContactPoint)==distance(*point) && nativeContactRank>*rank)))return;
                nativeContactPoint=point;nativeContactRank=*rank;contact.reset();t=0;
                nativeTerrainNormal=shape.rectangle?spiderRectangleNormal(lo,hi,*point,radius):
                    lo.x==hi.x?ContactVelocity{lo.x>=0?radius:-radius,0}:ContactVelocity{0,lo.z>=0?radius:-radius};
            };
            if(level_->nativeDecorTree.empty()) {
                for(size_t i=0;i<level_->nativeNavigationDecor.size();++i)decorContact(i);
            } else {
                const float minX=origin.x+std::min(0.f,step.x)-radius,maxX=origin.x+std::max(0.f,step.x)+radius;
                const float minZ=origin.y+std::min(0.f,step.y)-radius,maxZ=origin.y+std::max(0.f,step.y)+radius;
                for(size_t n=0;n<level_->nativeDecorTree.size();) {
                    const auto& node=level_->nativeDecorTree[n];
                    if(maxX<node.minX || node.maxX<minX || maxZ<node.minY || node.maxY<minZ){n=node.escape;continue;}
                    if(node.end-node.begin<=8)for(size_t j=node.begin;j<node.end;++j)decorContact(level_->nativeDecorOrder[j]);
                    ++n;
                }
            }
        } else if(level_->collisionEdgeTree.empty()) {
            for(size_t i=0;i<level_->collisionEdges.size();++i)edgeContact(i);
        } else {
            for(size_t n=0;n<level_->collisionEdgeTree.size();) {
                const auto& node=level_->collisionEdgeTree[n];
                if(x1<node.minX || node.maxX<x0 || y1<node.minY || node.maxY<y0){n=node.escape;continue;}
                if(node.end-node.begin<=8)for(size_t j=node.begin;j<node.end;++j)edgeContact(level_->collisionEdgeOrder[j]);
                ++n;
            }
        }
    }
    }
    // 4449f0 rolls back surface coordinates before436ac0 compares history.
    // Native bounds stay separate, including the return2 commit below.
    if(history && nativeContactPoint) {
        const auto rollback=spiderNativeToSurface(source.x,source.y);
        p.x=rollback.x;p.y=rollback.y;
        actors_[actor].enemyMovedLastStep=p.x!=history->previousSurface.x || p.y!=history->previousSurface.y;
    }
    //442825 Touch precedes timestamp/direction admission. It zeros Energy
    //without health/Hit/owner credit, then jumps directly to impact latches.
    //Death waits for the victim's own admitted visit; never spawn/reallocate here.
    std::optional<size_t> nativeTouchVictim;
    if(shared && contact) {
        const auto target=*contact;wakeNativeActor(target); //441b41 before Touch.
        if((nativeDamageFlags(actor)&0x400000u) && !(nativeDamageFlags(target)&0x200000u))nativeTouchVictim=target;
        else if((nativeDamageFlags(target)&0x400000u) && !(nativeDamageFlags(actor)&0x200000u))nativeTouchVictim=actor;
        if(nativeTouchVictim){actors_[*nativeTouchVictim].vitals.energy=0;actors_[*nativeTouchVictim].nativeDamagePending=true;}
    }
    const bool repeatedNativePair=contact && actors_[actor].contactResponseMs==elapsedMs_ &&
        actors_[*contact].contactResponseMs==elapsedMs_;
    if(nativeContactPoint && !nativeTouchVictim && !repeatedNativePair && !spiderContactAccepted(effectiveContactVelocity(actor),*nativeContactPoint)) {
        // Admission follows damage and timestamp selection in4406e0. Keep the
        // contact for damage below. Actor return2 skips its latch; terrain sets it.
        lastSpiderSeparated_=true;t=1;
    }
    const bool terrainReached=!contact;
    if(nativeTerrainNormal && !lastSpiderSeparated_)delta.constrained=true;
    if(contact && !lastSpiderSeparated_) {
        if(actor==player_)notifyAccessBlocked(*contact);
        // 4406e0 -> 4449f0 rolls a rejected Spider actor-contact attempt
        // back to its starting position before applying the contact response.
        // Moving to the swept contact point instead causes incremental drift.
        t=nativeBody
            ? 0.f : std::max(0.f,t-.0001f);
        delta.constrained=true;
    }
    delta.x*=t;delta.y*=t;
    if(history) {
        if(!nativeContactPoint){p.x=proposedSurface.x;p.y=proposedSurface.y;const auto previous=spiderNativeToSurface(source.x,source.y);history->previousSurface={previous.x,previous.y};}
        if(!nativeContactPoint || lastSpiderSeparated_)history->center={proposed.x,proposed.y};
    } else {p.x+=delta.x;p.y+=delta.y;}
    if(physical) {
        auto velocity=contactVelocity(actor);
        const auto attempted=surfaceToNative(dx,dy);
        //442704 reads cached+148, not hypot of provisional/response velocity.
        //Keep the accepted no-Ammo player/passive adapter outside this change.
        const float magnitude=nativePlayerPinball?motion_.magnitude:shared&&(nativeBody||actor==player_)?(actor==player_?motion_.magnitude:actors_[actor].enemyMagnitude):std::hypot(velocity.x,velocity.z);
        const float impact=magnitude*actors_[actor].contactConfig.weight;
        const bool damaging=impact>0 && std::abs(attempted.x)+std::abs(attempted.y)>2;
        const float elasticity=actor==player_ && surprise_.active(SurpriseKind::Pinball)?1.5f:actors_[actor].contactConfig.elasticity;
        if(contact) {
            const auto i=*contact;
            //441b41: selected actor wakes after caller admission, before the
            //response/separation branch, even when no impulse is applied.
            wakeNativeActor(i);
            const auto q=scene_->objects[i].position;
            const auto normal=nativeContactPoint?relativeNativeCenter(i):surfaceToNative(q.x-p.x,q.y-p.y);
            const auto& first=actors_[actor].contactConfig;const auto& second=actors_[i].contactConfig;
            // 4406e0 applies impact damage before the velocity response. Queue
            // destruction until actor iteration ends (explosions can grow arrays).
            if(damaging && !nativeTouchVictim) {
                if(actor!=player_ && (!actors_[actor].directFireOnly || i==player_))
                    contactDamage_.push_back({actor,impact*.5f,i,false});
                if(!actors_[i].directFireOnly || actor==player_)
                    contactDamage_.push_back({i,impact*.5f,actor,false});
            }
            const bool alreadyResponded=actors_[actor].contactResponseMs==elapsedMs_ && actors_[i].contactResponseMs==elapsedMs_;
            ActorContactVelocities response;bool writeFirst=true,writeSecond=true;
            if(!nativeTouchVictim && !alreadyResponded && !lastSpiderSeparated_) {
                if(nativeBody && !nativeSpider && nativeContactPoint) {
                    const auto native=resolveNativeActorContact(effectiveContactVelocity(actor),effectiveContactVelocity(i),
                        {},{normal.x,normal.y},*nativeContactPoint,first.weight,second.weight,elasticity,second.elasticity,
                        first.frictionX!=0,second.enabled && second.frictionX!=0);
                    response={native.first,native.second,native.applied};writeFirst=native.writeFirst;writeSecond=native.writeSecond;
                } else response=nativeSpider && nativeContactPoint?
                    resolveSpiderActorContact(effectiveContactVelocity(actor),effectiveContactVelocity(i),
                        {},{normal.x,normal.y},*nativeContactPoint,first.weight,second.weight,elasticity,second.elasticity,
                        first.frictionX!=0,second.enabled && second.frictionX!=0):
                    resolveActorContact(effectiveContactVelocity(actor),effectiveContactVelocity(i),{normal.x,normal.y},
                        first.weight,second.weight,elasticity,second.elasticity,second.enabled && second.frictionX!=0);
            }
            if(!nativeTouchVictim && !alreadyResponded){actors_[actor].contactResponseMs=elapsedMs_;actors_[i].contactResponseMs=elapsedMs_;}
            if(response.applied) {
                if(writeFirst)setContactVelocity(actor,response.first);
                if(writeSecond && second.enabled && second.frictionX!=0)setContactVelocity(i,response.second);
            }
            // 4406e0 sets impact latches even when +124 suppresses a second
            // response at the same timestamp, and even on friction-zero bodies.
            if((nativeBody || nativeTouchVictim) && !lastSpiderSeparated_) {
                if(actor!=player_ && !(nativeDamageSubtype(actor)>=0x11 && nativeDamageSubtype(actor)<=0x14))
                    actors_[actor].contactMotion=actors_[actor].brain.base!=BrainBase::Straight;
                if(i!=player_ && !(nativeDamageSubtype(i)>=0x11 && nativeDamageSubtype(i)<=0x14))
                    actors_[i].contactMotion=actors_[i].brain.base!=BrainBase::Straight;
            }
        } else if(nativeTerrainNormal) {
            //442752 emits before response admission (including separation).
            if(nativePlayerPinball && damaging && nativeContactPoint) {
                pinballBounceSource=p;
                pinballBouncePoint=ContentPosition{oldBody.x+nativeContactPoint->x,oldBody.z+nativeContactPoint->z};
            }
            if(damaging && actor!=player_ && !actors_[actor].directFireOnly) {
                if(actors_[actor].vitals.shield<=impact*.1f)actors_[actor].vitals.shield=0;
                else contactDamage_.push_back({actor,impact*.1f,std::nullopt,false});
            }
            // 4367d0 only changes velocity when436ac0 returned moving or
            // commanded; the impact latch is set even in the stationary case.
            const bool playerStationary=actor==player_ && nativePlayerPinball &&
                p.x==nativePlayerPreviousSurface_.x && p.y==nativePlayerPreviousSurface_.y;
            const bool commanded=actor==player_?
                (nativePlayerPinball?playerMoveCommanded_:motion_.thrust!=0):actors_[actor].enemyMoveCommand!=0;
            const bool moved=actor==player_?!playerStationary:actors_[actor].enemyMovedLastStep;
            if(!lastSpiderSeparated_ && (moved || commanded)) {
                const auto response=resolveNativeTerrainContact(effectiveContactVelocity(actor),
                    *nativeTerrainNormal,actor==player_?motion_.thrust:actors_[actor].enemyThrust,elasticity,commanded);
                if(response.applied)setContactVelocity(actor,response.velocity);
                if(response.clearCommand){actors_[actor].enemyMoveCommand=0;if(actor==player_)playerMoveCommanded_=false;}
            }
            // 4406e0 sets the terrain impact latch before4367d0, including
            // its separating-return2 path. Actor contacts have a different order.
            actors_[actor].contactMotion=actors_[actor].brain.base!=BrainBase::Straight;
        } else if(terrainReached && delta.constrained) {
            // Native wall impacts spare the player; other actors lose shield
            // only when this impact would exhaust it (no energy spill).
            if(damaging && actor!=player_ && !actors_[actor].directFireOnly) {
                if(actors_[actor].vitals.shield<=impact*.1f)actors_[actor].vitals.shield=0;
                else contactDamage_.push_back({actor,impact*.1f,std::nullopt,false});
            }
            if(nativePlayerPinball && terrain.count) {
                // 4105d0 chooses one closest contact;4367d0 responds once.
                // Retain swept geometry, selecting its closest contact to the
                // proposed center in the original native-circle metric.
                size_t selected=0;float closest=std::numeric_limits<float>::infinity();
                for(size_t n=0;n<terrain.count;++n){
                    const auto& c=terrain.values[n];
                    const auto d=surfaceToNative(c.x-(from.x+dx),c.y-(from.y+dy));
                    const float distance=d.x*d.x+d.y*d.y;
                    if(distance<closest){closest=distance;selected=n;}
                }
                //4449f0 rolls the whole blocked attempt back before436ac0.
                p=from;delta.x=delta.y=0;
                if(damaging) {
                    pinballBounceSource=p;
                    const auto& c=terrain.values[selected];
                    const auto point=spiderSurfaceToNative(c.x,c.y);
                    pinballBouncePoint=ContentPosition{point.x,point.y};
                }
                const bool moved=p.x!=nativePlayerPreviousSurface_.x || p.y!=nativePlayerPreviousSurface_.y;
                if(moved || playerMoveCommanded_) {
                    const auto& c=terrain.values[selected];
                    const auto response=resolveNativeTerrainContact(effectiveContactVelocity(actor),
                        {c.normalX,c.normalZ},motion_.thrust,elasticity,playerMoveCommanded_);
                    if(response.applied)setContactVelocity(actor,response.velocity);
                    if(response.clearCommand)playerMoveCommanded_=false;
                }
            } else {
                for(size_t n=0;n<terrain.count;++n)
                    velocity=reflectTerrainVelocity(velocity,{terrain.values[n].normalX,terrain.values[n].normalZ},elasticity);
                // Map bounds without an authored edge cannot supply a normal.
                if(terrain.count==0)velocity={};
                setContactVelocity(actor,velocity);
            }
        }
    }
    // Native rollback preserves the previous accepted movement comparison.
    if(history)actors_[actor].enemyMovedLastStep=p.x!=history->previousSurface.x || p.y!=history->previousSurface.y;
    else if(actors_[actor].enemy && !delta.constrained)actors_[actor].enemyMovedLastStep=p.x!=from.x || p.y!=from.y;
    if(!history)if(auto* retained=actors_[actor].nativeMotionHistory()) {
        const auto previous=spiderNativeToSurface(retained->center.x,retained->center.y);
        retained->previousSurface={previous.x,previous.y};
        const auto current=spiderSurfaceToNative(p.x,p.y);retained->center={current.x,current.y};
    }
    if(continuing && !nativeContactPoint){
        const auto subtype=nativeDamageSubtype(actor);
        const float factor=currentTickMilliseconds_>0?14.f/static_cast<float>(currentTickMilliseconds_):1.f;
        const auto velocity=subtype==0x19 || subtype==0x1f || subtype==0x20?ContactVelocity{}:
            ContactVelocity{float(static_cast<long double>(factor)*rawStep.x),
                            float(static_cast<long double>(factor)*rawStep.z)};
        if(actor==player_){motion_.velocityX=velocity.x;motion_.velocityZ=velocity.z;}
        else actors_[actor].contactVelocity=velocity;
    }
    if(nativePlayerPinball && !terrainMoveAllowed(*level_,from.x,from.y,p.x,p.y,radius)) {
        // Ammo continuation may use native endpoint placement. Preserve the
        // player's swept containment even when that query owns this visit.
        p=from;
        if(history){const auto n=spiderSurfaceToNative(p.x,p.y);history->center={n.x,n.y};history->previousSurface=from;}
        delta={0,0,true};
    }
    if(!sharedContact.used)contactAmmo(actor,from,p);
    if(!nativeTouchVictim)updateSpiderNavigation(actor,shared && (!nativeContactPoint || lastSpiderSeparated_)?sharedContact.closestNonContactNode:std::nullopt);
    //A separating return2 can leave bounds ahead of the rolled-back surface;
    //the next visit still needs the retained-center path.
    actors_[actor].nativeAmmoContinuation=continuing && lastSpiderSeparated_;
    if(actorBoundsActive_)updateActorBounds(actor);
    if(pinballBouncePoint) {
        // Capture the native pre-response event above; construct after local
        // reference use so factory growth cannot invalidate p/history. Response
        // and bounds bookkeeping do not consume RNG or construct other actors.
        Projectile source;source.classId=scene_->objects[actor].classId;source.visual=actor;
        source.slopeAltitude=actors_[actor].projectileAltitude.value_or(
            propertyNumber(scene_->objects[actor].properties,"Altitude"));
        spawnProjectileBounceEffect(source,pinballBounceSource,*pinballBouncePoint);
    }
    return delta;
}
void GameWorld::movePlayer(float dx,float dy) { if (player().alive) { move(player_,dx,dy);resolveAmmoContacts();for(const auto id:touchVictims_)destroy(id,player_);touchVictims_.clear();collect();updateLifts(); } }
size_t GameWorld::spawn(ClassId id,ContentPosition position) {
    if (scene_->objects.size()>=RuntimeActorLimit || id.value>=scene_->classes.size()) return std::numeric_limits<size_t>::max();
    const auto& cls=scene_->classes[id.value];
    ObjectPlacement object;
    object.id={static_cast<uint32_t>(scene_->objects.size())};object.classId=id;object.position=position;
    object.flags=cls.flags;object.sprite=cls.sprite;object.shadow=cls.shadow;
    object.behaviours=cls.behaviours;object.properties=cls.properties;
    const size_t created=object.id.value;
    scene_->objects.push_back(std::move(object));actors_.push_back(makeActor(scene_->objects.back()));
    initializeNativeIdentity(created);
    registerSavingPrice(created);
    if(actors_[created].contactMine)contactMines_.push_back(created);
    audioEvent(AudioEventKind::Spawn,created,cls.name);
    if(!constructingScene_) {
        if(!spiderNavigation_ && actors_[created].contactConfig.nativeEnemy)initializeSpiderNavigation();
        else updateSpiderNavigation(created,{},false);
    }
    updateActorCreates(created,position,true);
    synchronizeActorCell(created);
    return created;
}
void GameWorld::expireVisual(size_t id) {
    if (id<actors_.size() && actors_[id].effect && !actors_[id].explosion) {
        actors_[id].alive=false;actors_[id].solid=false;
        releaseNativeActorIdentity(id);
        synchronizeActorCell(id);
        if(actors_[id].pooledTrail)freeTrailSlots_[scene_->objects[id].classId.value].insert(id);
    }
}
void GameWorld::destroy(size_t id,std::optional<size_t> source) {
    if (id>=actors_.size() || !actors_[id].alive) return;
    const auto capturedCreatorGeneration=actors_[id].nativeCreatorGeneration;
    actors_[id].nativeDamagePending=false;
    clearOrdinaryEnemyRoutes(id);
    // 440a31: authored Two-pieces Turret/Tank loses its upper piece before
    // the retained lower piece. Only the second transition releases drops.
    const auto family=objectSubtype(scene_->objects[id]);
    const bool topPhase=(family=="turret" || family=="tank") &&
        propertyBool(scene_->objects[id].properties,"Two-pieces") && !actors_[id].turretOnlyBase;
    clearSpiderNavigationRoutes(id);
    if(!topPhase){actors_[id].alive=false;actors_[id].solid=false;++actorsDestroyed;}
    updateSpiderNavigation(id);
    if(actors_[id].pooledTrail)freeTrailSlots_[scene_->objects[id].classId.value].insert(id);
    if(const auto parent=actors_[id].generatorParent;!topPhase && parent && *parent<actors_.size() && actors_[*parent].alive)
        for(auto& generator:actors_[*parent].generators)
            if(generator.stablePopulation && generator.product==scene_->objects[id].classId)--generator.birthCount;
    if(id==player_){actors_[id].nativeHit=false;map_.close();cancelSurprise();}
    if(source==player_ && id!=player_ && !actors_[id].nativeKillCounted && !actors_[id].turretOnlyBase && killCreditEligible(scene_->objects[id]))++playerKills_;
    const auto object=scene_->objects[id]; // spawning may reallocate both arrays
    // 4406e0 announces the zero transition regardless of kill ownership.
    // The original only displays100000 here. User-requested quirk fix pays
    // that advertised amount once on the same native counter transition.
    if(actors_[id].hudEnemy && --bonusEnemies_==0) {
        applyPickup(actors_[player_].vitals,{0,0,100000});
        notifications_.lastEnemy(playerName_);
    }
    // 413d80 parser keyword0x3a (Reward) -> actor+0x108; 4406e0 pays positive
    // rewards for native actor kinds22..32 regardless
    // of killer ownership. The ownership-aware kill statistic above is separate.
    const auto reward=static_cast<int64_t>(std::clamp<double>(propertyNumber(object.properties,"Reward"),0,INT32_MAX));
    if(id!=player_ && !actors_[id].pickup && killCreditEligible(object) && reward>0) {
        applyPickup(actors_[player_].vitals,{0,0,reward});
        notifications_.add({NoticeKind::Reward,std::to_string(reward)+" $",
            object.position.x,object.position.y,-1.5f,2000,0,true,true,2,5,0xf0ff10});
        notifications_.add({NoticeKind::ActorName,propertyText(object.properties,"Name",scene_->classes[object.classId.value].name),
            object.position.x-32,object.position.y-32,-1,2000,0,true,true,6,5});
    }
    if(topPhase) {
        auto& actor=actors_[id];
        actor.turretOnlyBase=true;
        actor.vitals.energy=propertyNumber(children(scene_->classes[object.classId.value].properties,"Energy"),"Initial",1);
        actor.vitals.shield=propertyNumber(children(scene_->classes[object.classId.value].properties,"Shield"),"Initial");
        actor.shieldUpStep=0; // 440aaa: converted lower piece stops regeneration.
        actor.brain.base=BrainBase::Fixed;actor.brain.seeker=actor.brain.tracker=false;
        // 44087c..440889 counts the first/top destruction; 4371b0 rejects
        // the remaining Fixed-only base, including a resumed checkpoint.
        actor.hudEnemy=false;
        actor.brainState.acquired=false;actor.enemyRevealed=false;
        if(family=="turret")actor.enemyMoveCommand=0;
        // 42c340 drops retained route/action references, not hull momentum,
        // turn poses or weapon stocks. A Tank lower piece can still coast.
        actor.route.clear();actor.routePoint=0;actor.routeGoal.reset();actor.routeSearch.reset();
        // The existing saved object properties carry this runtime native bit.
        // This derived bit needs no additional portable actor field.
        auto& properties=scene_->objects[id].properties;
        std::erase_if(properties,[](const auto& n){return contentNameIs(n.key,"only-base");});
        TextNode base;base.kind=TextKind::Assignment;base.key="Only-base";base.values={"1"};
        properties.push_back(std::move(base));
        configureOrdinaryEnemyAI(actor,scene_->objects[id],true);
        const auto creator=object.owner?std::optional<size_t>(object.owner->value):std::nullopt;
        // 437d40 offsets non-Z-shadow first-phase FX surface Y by4.
        spawnExplosions(object,creator,actor.projectileAltitude.value_or(propertyNumber(object.properties,"Altitude")),true,capturedCreatorGeneration);
        audioEvent(AudioEventKind::Death,id);
        return;
    }
    DeathDropScatter scatter;
    // Class parser40a010 prepends each template. 437f80 walks HAVE first,
    // then independently rolls every RANDOM entry; neither is a weighted pool.
    if(id!=player_)for(const auto kind:{"have","random"}) {
        scatter.beginList();
        for(auto it=object.properties.rbegin();it!=object.properties.rend();++it) {
            const auto& node=*it;
            // 40a010 keyword34 clears inherited HAVE templates. Retained
            // markers let us honor that rule without changing old save hashes.
            if(contentNameIs(kind,"have") && contentNameIs(node.key,"override-objects"))break;
            if(!contentNameIs(node.key,kind) || node.arguments.empty())continue;
            const auto* cls=findClass(*scene_,node.arguments.front());
            if(!cls){diagnostics_.push_back(std::string("unresolved ")+kind+": "+node.arguments.front());continue;}
            if(contentNameIs(node.key,"random")) {
                const auto numerator=node.arguments.size()==3?deathDropInteger(node.arguments[1]):std::nullopt;
                const auto denominator=node.arguments.size()==3?deathDropInteger(node.arguments[2]):std::nullopt;
                if(!numerator || !denominator){diagnostics_.push_back("invalid RANDOM: "+node.arguments.front());continue;}
                // Even certain/impossible authored entries consume their roll.
                if((effectCamera_ && (effectCamera_->modeFlags&0x18000)) ||
                   !nativeDeathRandomDrop(nativeGlobalRandom_,*numerator,*denominator))continue;
            }
            spawn(cls->id,scatter.next(object.position));
        }
    }
    if(id!=player_)releaseHeldItems(id,scatter);
    const auto creator=object.owner?std::optional<size_t>(object.owner->value):std::nullopt;
    spawnExplosions(object,creator,actors_[id].projectileAltitude.value_or(propertyNumber(object.properties,"Altitude")),false,capturedCreatorGeneration);
    audioEvent(AudioEventKind::Death,id);
    if (id==player_) message_="Vyruz destroyed — R to restart";
    if(actors_[id].reactivateMs<0)releaseNativeActorIdentity(id);
}
bool GameWorld::playerOwnsDamage(std::optional<size_t> creator) const {
    // Callers supply native damage actor's creator, already one ancestry hop in.
    for(size_t depth=0;creator && *creator<actors_.size() && depth<actors_.size();++depth) {
        if(*creator==player_)return true;
        const auto owner=scene_->objects[*creator].owner;
        creator=owner?std::optional<size_t>(owner->value):std::nullopt;
    }
    return false;
}
void GameWorld::updateCombatBonus(double milliseconds) {
    if(milliseconds<fastKillMs_){fastKillMs_-=milliseconds;return;}
    if(!player().alive)return;
    fastKillMs_=0;
    if(fastKillCount_>1) {
        const int64_t reward=static_cast<int64_t>(std::min<uint64_t>(fastKillCount_,INT64_MAX/5000))*5000;
        applyPickup(actors_[player_].vitals,{0,0,reward});
        notifications_.fastKill(playerName_,reward);
        static const char* voices[]={"V-WELL-DONE.WAV","V-YEEPEE.WAV","V-COME-ON.WAV","V-ARIBA.WAV","V-LAUGH-1.WAV","V-THE-BEST.WAV","V-WANT-MORE.WAV","V-HEY.WAV"};
        voiceEvent(voices[static_cast<uint64_t>(elapsedMs_)&7],500,1000,5000);
    }
    fastKillCount_=0;
}
void GameWorld::radialInjuryCommentary(float amount) {
    //43cee4..43cf56: applied radial amount, not lost shield/energy. A shield
    //absorbing the entire hit still qualifies; contact damage does not.
    if(!(amount>=300) || !player().alive || !(player().vitals.energy>0) ||
       (nativeDamageFlags(player_)&0x00a00000))return;
    static const char* names[]={"V-OOH.WAV","V-LOOSER.WAV","V-SAD-MAN.WAV"};
    const unsigned choice=static_cast<uint64_t>(elapsedMs_)&7;
    if(choice<std::size(names))voiceEvent(names[choice],500,2000,5000);
}
void GameWorld::damage(size_t id,float amount,std::optional<size_t> source,bool radial,bool ammoSource) {
    damageQueue_.push_back({id,amount,source,radial,ammoSource});
    if (applyingDamage_) return;
    applyingDamage_=true;
    for (size_t next=0;next<damageQueue_.size();++next) {
        const auto [target,value,owner,area,fromAmmo]=damageQueue_[next];
        if (target>=actors_.size() || !actors_[target].alive || actors_[target].pickup) continue;
        if(actors_[target].timedProtection.active)continue;
        if(actors_[target].directFireOnly && !playerOwnsDamage(owner))continue;
        if(target==player_ && (debugGodMode_ || surprise_.active(SurpriseKind::Invulnerable)))continue;
        if (target==player_ && scene_->objects[target].flags.invisible.value_or(false)) continue;
        //Health alone never sets Hit. Radial producers latch independently of
        //shield loss; actual player rendering consumes the pending latch.
        if(area && value!=0 && std::isfinite(value))actors_[target].nativeHit=true;
        const bool killed=applyDamage(actors_[target].vitals,value,actors_[target].indestructible);
        if(area && target==player_)radialInjuryCommentary(value);
        if (killed) {
            // 43cb20: hostile deaths caused by player-owned radial damage.
            // Touch/lifetime/script destruction does not enter this path.
            if(area && actors_[target].hudEnemy && !actors_[target].turretOnlyBase && playerOwnsDamage(owner)) {
                if(fastKillMs_<1){fastKillMs_=500;fastKillCount_=1;}
                else ++fastKillCount_;
            }
            if(area && fromAmmo && owner==player_ && target!=player_) {
                // 43d070: occasional commentary, independent of the reward.
                static const char* voices[]={"V-YEAH.WAV","V-HEY.WAV","V-YO.WAV","V-GIVE-ME.WAV","V-THE-BEST.WAV","V-WANT-MORE.WAV","V-LAUGH-1.WAV"};
                const unsigned choice=static_cast<uint64_t>(elapsedMs_)&15;
                if(choice<std::size(voices))voiceEvent(voices[choice],1000,1500);
            }
            const bool finalObject=actors_[target].finalObject;
            // 43ca30 reports the lethal final hit before4406e0 destroys it.
            // Clear existing actors first: the newly spawned finale FX must
            // survive the clear and complete their authored animation/CREATE.
            if(finalObject)beginFinalEnding(target);
            destroy(target,owner);
        }
    }
    damageQueue_.clear();applyingDamage_=false;
}
void GameWorld::collect() {
    if(endingSource_)return; // native final destruction sequence disables pickups
    if (!player().alive || scene_->objects[player_].flags.invisible.value_or(false)) return;
    for (size_t i=0;i<actors_.size();++i) {
        auto& item=actors_[i];
        if (i==player_ || !item.alive || !item.pickup || item.pickupDelayMs>0) continue;
        const auto p=playerPosition(),q=scene_->objects[i].position;
        if (nativeDistanceSquared(p.x-q.x,p.y-q.y)>std::pow(player().radius+item.radius,2)) continue;
        const auto* surpriseItem=attribute(scene_->objects[i].properties,"item");
        if(scene_->objects[i].flags.invisible.value_or(false) &&
           !propertyBool(scene_->objects[i].properties,"Keep-bb-when-invisible"))continue;
        const auto surpriseKind=surpriseItem?surpriseParse(folded(propertyText(surpriseItem->children,"Surprise-type"))):SurpriseKind::None;
        if(surpriseKind!=SurpriseKind::None && surprise_.kind!=SurpriseKind::None)continue;
        auto& collector=actors_[player_];
        const auto before=collector.vitals;
        const bool carry=propertyBool(scene_->objects[i].properties,"Carry");
        bool taken=applyPickup(collector.vitals,item.pickupEffect) || carry;
        if(surpriseKind!=SurpriseKind::None)taken|=takeSurprise(surpriseKind);
        float noticeY=q.y;
        bool priorNotice=false;
        const auto pickupNotice=[&](const std::string& text,uint32_t color,bool stack=false) {
            if(stack && priorNotice)noticeY-=20;
            notifications_.add({NoticeKind::Pickup,text,q.x-32,noticeY,-1,2000,0,true,true,6,5,color});
            priorNotice=true;
        };
        // 4406e0 prints authored grants, including when the actual gain is capped.
        if(collector.vitals.energy>before.energy)
            pickupNotice(std::string(notifications_.language()==2?"Energie +":"Energy +")+std::to_string(static_cast<int>(item.pickupEffect.energy)),0x80ff20);
        if(collector.vitals.money>before.money) {
            pickupNotice(std::string(notifications_.language()==2?"Argent +":"Money +")+std::to_string(item.pickupEffect.money),0xffc040);
            // 4406e0 money branch: six quips out of sixteen clock residues.
            static const char* voices[]={"V-LAUGH-1.WAV","V-YEEPEE.WAV","V-YEAH.WAV","V-HEY.WAV","V-YO.WAV","V-GIVE-ME.WAV"};
            const unsigned choice=static_cast<uint64_t>(elapsedMs_)&15;
            if(choice<std::size(voices))voiceEvent(voices[choice]);
        }
        if(collector.vitals.shield>before.shield)
            pickupNotice(std::string(notifications_.language()==2?"Bouclier +":"Shield +")+std::to_string(static_cast<int>(item.pickupEffect.shield)),0xff4020,true);
        const auto& properties=scene_->objects[i].properties;
        const auto collectAmmo=[&](const std::string& label,uint32_t amount) {
            const auto name=folded(label);
            // 444540 admits a stock only through a carried ADD Weapon whose
            // class has a compatible TYPE Weapon; an orphan ADD Ammo slot
            // alone does not make the corresponding pickup usable.
            bool compatible=false;
            for(const auto& weapon:collector.weapons) {
                const auto* cls=findClass(*scene_,weapon.name);if(!cls)continue;
                for(const auto& variant:cls->properties)
                    if(contentNameIs(variant.key,"type") && !variant.arguments.empty() &&
                       contentNameIs(variant.arguments.front(),"weapon") &&
                       contentNameEqual(propertyText(variant.children,"Ammo"),name)) {compatible=true;break;}
                if(compatible)break;
            }
            if(!compatible)return;
            // 444540 visits the first matching nonfull record and stops even
            // when more ammunition would fit in a later duplicate.
            auto& stocks=surprise_.active(SurpriseKind::Ammo)?surprise_.savedAmmo:collector.ammo;
            for(auto& [stockName,stock]:stocks) {
                if(stockName!=name)continue;
                auto& value=stock.count;
                if(value>=stock.maximum)continue;
                const auto add=std::min(amount,stock.maximum-value);value+=add;taken|=add>0;
                if(add)pickupNotice(label+" +"+std::to_string(amount),0xffffff,true);
                break;
            }
        };
        if(const auto* effect=attribute(properties,"item"))
            collectAmmo(propertyText(effect->children,"Ammo-Type"),count(propertyNumber(effect->children,"Add-Ammo")));
        for(const auto& n:properties)if(folded(n.key)=="add" && !n.arguments.empty() && folded(n.arguments.front())=="ammo")
            collectAmmo(propertyText(n.children,"Name"),count(propertyNumber(n.children,"Count")));
        if (const auto* key=attribute(properties,"key")) {
            const int code=static_cast<int>(propertyNumber(key->children,"Id"));
            if (code) {
                collector.heldKeys.push_back({code,propertyBool(key->children,"Is-code"),
                    static_cast<int>(propertyNumber(key->children,"Code-part-id",1)),
                    static_cast<int>(propertyNumber(key->children,"Code-part-count",1)),
                    propertyText(key->children,"Code-String"),{}});
                collector.heldKeys.back().held=retainCode(i);
                if(carry && surpriseKind==SurpriseKind::None) {
                    const auto& picked=collector.heldKeys.back();
                    notifications_.keyPicked(code,picked.code,picked.part,hasKey(collector.heldKeys,code));
                    priorNotice=true;
                }
                taken=true;
            }
        }
        if (taken) {
            audioEvent(AudioEventKind::Pickup,i,scene_->classes[scene_->objects[i].classId.value].name);
            if(surpriseKind!=SurpriseKind::None) {
                const auto resolved=surprise_.kind;
                if(resolved!=SurpriseKind::Nop)audioEvent(static_cast<unsigned>(resolved)<=10?AudioEventKind::Malus:AudioEventKind::Bonus,i);
            }
            if(!priorNotice)message_="Picked up "+scene_->classes[scene_->objects[i].classId.value].name;
            ++pickupsTaken;
            // 444660 destroys ordinary consumables through437d40. Carry
            // objects are removed from the ground instead; only that branch
            // may schedule Reactivate-after (codes have their own return path).
            if(carry) {
                retainItem(i,surpriseKind!=SurpriseKind::None);
                item.alive=false;item.reactivateMs=-1;
                synchronizeActorCell(i);
                const auto* key=attribute(properties,"key");
                const float delay=surpriseItem?propertyNumber(surpriseItem->children,"Reactivate-after",-1):-1;
                if(delay>0 && !(key && propertyBool(key->children,"Is-code"))) {
                    // 444660 -> 433ff0: the held instance stays removed. The
                    // return is a fresh class-default object at its current
                    // position, with the collected instance's delay. Spawning
                    // may reallocate both arrays: do not reuse item/properties.
                    const auto classId=scene_->objects[i].classId;
                    const auto position=scene_->objects[i].position;
                    const auto replacement=spawn(classId,position);
                    if(replacement<actors_.size()) {
                        actors_[replacement].alive=false;
                        actors_[replacement].ageMs=0;
                        actors_[replacement].reactivateMs=delay;
                        synchronizeActorCell(replacement);
                    }
                }
            } else destroy(i,std::nullopt);
        }
    }
}
void GameWorld::selectCompatibleActorAmmo(size_t id) {
    auto& actor=actors_[id];
    selectCompatibleAmmo(actor.weapons[actor.selectedWeapon],actor,scene_->objects[id],*scene_);
}
void GameWorld::fire(size_t id,bool held,bool sweep,std::optional<ContentPosition> forwardPrevious,std::optional<bool> aiming) {
    if(id<actors_.size())actors_[id].nativeFireRequested=held;
    if(deferActorFire_){deferredActorFire_=PendingActorFire{id,held,sweep,forwardPrevious,aiming};return;}
    if(id==player_ && (surprise_.jammed || surprise_.active(SurpriseKind::Motor)))held=false;
    auto& actor=actors_[id];
    if (!actor.alive || actor.weapons.empty() || std::find(touchVictims_.begin(),touchVictims_.end(),id)!=touchVictims_.end()) return;
    if(id==player_ && held) {
        const auto* selectedClass=findClass(*scene_,actor.weapons[actor.selectedWeapon].ammo);
        const auto* selectedAmmo=selectedClass?attribute(selectedClass->properties,"ammo"):nullptr;
        // 443bd0 checks Invisible/Usable-when-invisible before444450.
        if(!surprise_.active(SurpriseKind::Drone) ||
           (selectedAmmo && propertyBool(selectedAmmo->children,"Usable-when-invisible")))
            selectLoadedPlayerWeapon();
    }
    auto& weapon=actor.weapons[actor.selectedWeapon];
    auto stock=weaponStock(actor,weapon);
    if(stock==actor.ammo.end() || !stock->second.count) {
        selectCompatibleAmmo(weapon,actor,scene_->objects[id],*scene_);
        stock=weaponStock(actor,weapon);
    }
    if (stock==actor.ammo.end()) return;
    const auto* cls=findClass(*scene_,weapon.ammo);
    const auto* ammo=cls?attribute(cls->properties,"ammo"):nullptr;
    if (!ammo) return;
    if(id==player_ && surprise_.active(SurpriseKind::Drone) && !propertyBool(ammo->children,"Usable-when-invisible")) {
        if(held)notifications_.greenInfo(NoticeKind::Warning,"NOTE : ONLY MINES CAN BE USED WHILE INVISIBLE",100,110,2000,true);
        held=false;
    }
    if(!held || !stock->second.count)audioEvent(AudioEventKind::WeaponStop,id,weapon.name);
    if(held && !stock->second.count)audioEvent(AudioEventKind::DryFire,id,weapon.name);
    const auto beforeAmmo=stock->second.count;
    if(sweep)weapon.state.lastFireMs.reset();
    auto requests=fireWeapon(weapon.config,weapon.state,held,static_cast<uint64_t>(elapsedMs_),stock->second.count,aiming,actor.nativeAmmoOwner.activeAmmo);
    std::vector<ShotPositionOffset> shotOrigins(requests.size());
    if(id==player_ && !requests.empty()) {
        if(surprise_.active(SurpriseKind::Triple) || surprise_.active(SurpriseKind::Circular) || sweep)
            stock->second.count=beforeAmmo-((!sweep || surprise_.circularRemaining==1)?1:0);
        if(surprise_.tripleOffset>0 && stock->second.count>0) {
            const auto originals=requests;requests.clear();shotOrigins.clear();
            for(const auto& request:originals)for(int side:{-1,1,0}) {
                requests.push_back(request);
                shotOrigins.push_back(tripleFireOffset(actor.direction,surprise_.tripleOffset,side));
            }
        }
        if(surprise_.circularRemaining>0){--surprise_.circularRemaining;}
    } else if(id==player_ && sweep)surprise_.circularRemaining=0;
    if(requests.empty())return;
    audioEvent(AudioEventKind::WeaponFire,id,weapon.name);
    if (!requests.empty() && weapon.visibleGunFire) ++actor.fireRevision;
    const int firingDirection=actor.weaponHeading();
    // 443bd0 uses the same PE heading table for every emitter, including
    // ordinary Cop/Gun/Laser/Rocket ammo. A circle in surface coordinates
    // points up to19 degrees away from this native isometric heading.
    const bool nativeEnemyLaunch=id!=player_;
    const auto heading=nativeHeading(firingDirection);
    const auto position=scene_->objects[id].position;
    // Each body/muzzle product may reallocate actors_. Snapshot emitter inputs
    // before the first barrel so later barrels never read an invalid reference.
    const auto shooterMomentum=actor.contactVelocity;
    const int shooterMoveCommand=actor.enemyMoveCommand;
    const auto* shooterHistory=actor.nativeMotionHistory();
    const auto shooterPrevious=shooterHistory?std::optional<ContentPosition>{shooterHistory->previousSurface}:std::nullopt;
    const auto range=weapon.range;
    const bool slopeEnabled=weapon.config.initialSlope<0;
    const float slopeRadians=weapon.state.slopeRadians;
    float startDistance=0;
    const float muzzleSpan=actor.radius*2;
    float launchAltitude=actor.projectileAltitude.value_or(propertyNumber(scene_->objects[id].properties,"Altitude"));
    const float shooterAltitude=launchAltitude;
    std::optional<ClassId> smokeWeapon;
    if(!requests.empty()) {
        const auto* gun=findClass(*scene_,weapon.name);
        const auto* metadata=gun?weaponMetadata(*gun):nullptr;
        if(metadata) {
            smokeWeapon=gun->id;
            startDistance=propertyNumber(metadata->children,"Start-distance");
            const float fireAltitude=propertyNumber(metadata->children,"Fire-altitude");
            if(fireAltitude!=0)launchAltitude=fireAltitude;
        }
    }
    // 443fc4..443fee: In-ground forces0; otherwise explicit Fire-altitude or
    // shooter runtime altitude replaces the ammunition class's initial height.
    if(cls->flags.inGround.value_or(false))launchAltitude=0;
    for (size_t shotIndex=0;shotIndex<requests.size();++shotIndex) {
        const auto& shot=requests[shotIndex];
        const auto origin=shotOrigins[shotIndex];
        const auto start=projectileStartOffset(firingDirection,nativeWeaponStartDistance(
            startDistance,muzzleSpan,muzzleSpan,effectCamera_?effectCamera_->modeFlags:0,nativeWorldRandom_));
        Projectile projectile;
        projectile.owner=id;projectile.classId=cls->id;
        projectile.slopeEnabled=slopeEnabled;projectile.slopeRadians=slopeRadians;
        projectile.slopeAltitude=std::trunc(launchAltitude);projectile.slopeHeight=projectile.slopeAltitude;
        float nx=heading.x,nz=heading.y;
        const float norm=std::hypot(nx,nz);nx/=norm;nz/=norm;
        const auto side=id==player_?ShotPositionOffset{(-nz-nx)*.8944272f*shot.lateralOffset,
                                                       (-nz+nx)*.4472136f*shot.lateralOffset}
                                  :enemyProjectileBarrelOffset(firingDirection,shot.lateralOffset);
        projectile.position={position.x+start.x+origin.x+side.x,
                             position.y+start.y+origin.y+side.y};
        projectile.directionX=nx;projectile.directionZ=nz;
        initializeProjectileGuidance(projectile,id);
        auto& config=projectile.motionConfig;
        config.speedMax=propertyNumber(cls->properties,"Speed-max",300);
        config.weight=std::max(.001f,propertyNumber(children(cls->properties,"Weight"),"Initial",1));
        config.acceleration=propertyNumber(cls->properties,"Acceleration-step");
        config.deceleration=propertyNumber(cls->properties,"Decceleration-step");
        config.grip=propertyNumber(cls->properties,"Grip",1);
        config.inertia=propertyBool(cls->properties,"Use-inertia");
        config.launched=propertyBool(cls->properties,"Launched");
        const auto axis=[&](const char* name,size_t index,float fallback) {
            const auto* n=contentProperty(cls->properties,name);
            if (!n || index>=n->values.size()) return fallback;
            char* end=nullptr;const float v=std::strtof(n->values[index].c_str(),&end);
            return end!=n->values[index].c_str() && std::isfinite(v)?v:fallback;
        };
        config.frictionX=axis("Friction-factor",0,.97f);config.frictionZ=axis("Friction-factor",1,config.frictionX);
        config.thresholdX=axis("Friction-threshold",0,.1f);config.thresholdZ=axis("Friction-threshold",1,config.thresholdX);
        projectile.motion.thrust=propertyNumber(children(cls->properties,"Thrust"),"Initial",config.speedMax);
        const bool nativePlayerLaunch=id==player_ &&
            (contentNameIs(cls->name,"missile-ammo-vz") || contentNameIs(cls->name,"rocket-ammo") || nativePulsarAmmo(*cls) || nativeStingerFlameSawAmmo(*cls) || contentNameIs(cls->name,"grenade-ammo"));
        if(nativeEnemyLaunch || nativePlayerLaunch) {
            // 4441fb..44423c: native initialization irrespective of Launched.
            // Every enemy emitter; accepted scoped player launch adapters.
            projectile.motion.velocityX=heading.x*projectile.motion.thrust+(nativePlayerLaunch?motion_.velocityX:shooterMomentum.x);
            projectile.motion.velocityZ=heading.y*projectile.motion.thrust+(nativePlayerLaunch?motion_.velocityZ:shooterMomentum.z);
            std::optional<ContentPosition> previous=nativePlayerLaunch?forwardPrevious:std::nullopt;
            if(!nativePlayerLaunch && shooterMoveCommand==200)previous=shooterPrevious;
            if(previous) {
                // 44424e..44429a retains X extended, spills scaled Y and
                // projected X, then adds projected Z before its float store.
                const long double sx=((long double)position.x-previous->x)*.015625L;
                const float sy=float(((long double)position.y-previous->y)*.03125L);
                constexpr float scale=35.777088165283203125f;
                const float extraX=float((sx+sy)*scale);
                const long double extraZ=((long double)sy-sx)*scale;
                projectile.motion.velocityX+=extraX;
                projectile.motion.velocityZ=float(extraZ+projectile.motion.velocityZ);
            }
            // The emitter does not replace the newly initialized magnitude
            // cache with hypot(velocity);438b20 updates it after propulsion.
        } else if (config.launched) {
            projectile.motion.velocityX=nx*.1f*projectile.motion.thrust;
            projectile.motion.velocityZ=nz*.1f*projectile.motion.thrust;
            if (id==player_) {
                projectile.motion.velocityX+=motion_.velocityX;
                projectile.motion.velocityZ+=motion_.velocityZ;
            }
            projectile.motion.magnitude=std::hypot(projectile.motion.velocityX,projectile.motion.velocityZ);
        }
        projectile.range=range;projectile.radius=std::max(0.f,propertyNumber(cls->properties,"Bounding-box-size",1));
        projectile.damage=propertyNumber(ammo->children,"damage");
        projectile.damageRadius=propertyNumber(ammo->children,"damage-zone",1);
        projectile.activeAfterMs=propertyNumber(ammo->children,"active-after");
        projectile.fuseMs=propertyNumber(ammo->children,"damage-after");
        projectile.ignoreOwner=propertyBool(ammo->children,"Ignore-owner");
        projectile.lifetimeMs=propertyNumber(cls->properties,"Life-time",-1);
        projectile.hitEffect=propertyText(ammo->children,"hit-fx");
        projectile.verticalBounce=propertyBool(cls->properties,"Vertical-Bounce");
        projectile.bouncePeriod=propertyNumber(cls->properties,"Bounce-period");
        //441b41: On-hit/On-object-hit select contact policy independently
        //of Vertical/Horizontal-Bounce presentation (Saw has both clear).
        projectile.terrainBounce=nativeAmmoTerrainResponse(propertyBool(ammo->children,"on-hit"),
                                                           propertyBool(ammo->children,"on-object-hit"));
        projectile.bounceElasticity=propertyNumber(cls->properties,"Elasticity");
        // Projectile already represents invisible bare bullets. Allocate an
        // actor only for rendered bodies or actor-dependent secondary effects.
        const bool createdEffects=std::any_of(cls->properties.begin(),cls->properties.end(),
            [](const auto& node){return folded(node.key)=="create";});
        if ((cls->sprite.sequence.value && !cls->flags.invisible.value_or(false)) ||
            attribute(cls->properties,"explosion") || createdEffects) {
            const auto visual=spawn(cls->id,projectile.position);
            if (visual<actors_.size()) {projectile.visual=visual;actors_[visual].solid=false;actors_[visual].enemy=false;actors_[visual].directional=true;actors_[visual].projectileAltitude=launchAltitude;actors_[visual].ammoContactDisabled=!ammoContactEnabled(projectile);}
        }
        configureProjectileCreates(projectile,true);
        initializeBareAmmoBody(projectile);
        //4440a5 links the armed parent before Smoke-fx construction; owner
        //capture and launch heading follow Smoke at4441ef/4442d9.
        projectile.owner=SIZE_MAX;projectile.nativeOwnerGeneration=0;
        if(projectile.visual)assignNativeCreator(*projectile.visual,{});
        const auto launchedMotion=projectile.motion;const auto launchedVelocity=projectile.velocity;
        const auto launchedDirectionX=projectile.directionX,launchedDirectionZ=projectile.directionZ;
        projectile.motion.velocityX=projectile.motion.velocityZ=projectile.motion.magnitude=0;
        projectile.velocity={};
        const auto initialHeading=nativeHeading(projectile.visual?actors_[*projectile.visual].direction:0);
        const auto initialLength=std::hypot(initialHeading.x,initialHeading.y);
        projectile.directionX=initialHeading.x/initialLength;projectile.directionZ=initialHeading.y/initialLength;
        nativeAmmoOwnerLaunched(actors_[id].nativeAmmoOwner); //444076, before parent cell and Smoke.
        const auto emitted=projectiles_.size();projectiles_.push_back(std::move(projectile));
        if(projectiles_[emitted].visual)visualAmmoIndex_[*projectiles_[emitted].visual]=emitted;
        if(projectiles_[emitted].nativeBodyIdentity)bareAmmoIndex_[projectiles_[emitted].nativeBodyIdentity]=emitted;
        synchronizeBareAmmoCell(projectiles_[emitted]);
        if(smokeWeapon)spawnWeaponSmoke(*smokeWeapon,projectiles_[emitted].position,shooterAltitude);
        ++shotsFired; //4441de, after Smoke and before creator/momentum.
        projectiles_[emitted].owner=id;projectiles_[emitted].nativeOwnerGeneration=actors_[id].nativeGeneration;
        if(projectiles_[emitted].visual){const auto visual=*projectiles_[emitted].visual;
            assignNativeCreator(visual,id);actors_[visual].direction=firingDirection;}
        projectiles_[emitted].motion=launchedMotion;projectiles_[emitted].velocity=launchedVelocity;
        projectiles_[emitted].directionX=launchedDirectionX;projectiles_[emitted].directionZ=launchedDirectionZ;
        updateSpiderProjectileNavigation(projectiles_[emitted]);
    }
    if(id==player_ && !requests.empty() && (sweep || surprise_.active(SurpriseKind::Circular))) {
        auto facing=nativePlayerFacing_;setPlayerDirection((firingDirection+1)%24);
        facing.logicalHeading=(facing.logicalHeading+1)%24;facing.orthogonalHeading=(facing.logicalHeading+3)%24;
        facing.pose+=2;if(facing.pose>48)facing.pose=1;nativePlayerFacing_=facing;
    }
    // Native emitter repeats444450 immediately after consuming the last round.
    if(id==player_ && !requests.empty())selectLoadedPlayerWeapon();
}
void GameWorld::updateProjectile(size_t index,float seconds,bool nativeRouting) {
    auto& shot=projectiles_[index];
    const auto syncNavigation=[&](Projectile& shot){
        if(shot.visual)updateSpiderNavigation(*shot.visual);
        updateSpiderProjectileNavigation(shot);
    };
    if (!shot.alive) {if(nativeRouting)syncNavigation(shot);return;}
    if (shot.visual && !actors_[*shot.visual].alive) { shot.alive=false;if(nativeRouting)syncNavigation(shot);return; }
    shot.ageMs+=seconds*1000;
    if(shot.visual)actors_[*shot.visual].ageMs=shot.ageMs;
    // 4406e0 checks nonzero class LifeTime before motion and CREATE.
    // Damage-after detonation follows a different path and may emit a
    // final due trail. Zero class lifetime means unlimited.
    if(shot.lifetimeMs>0 && shot.ageMs>=shot.lifetimeMs) {
        shot.alive=false;
        if(shot.visual)destroy(*shot.visual,shot.owner);
        else {
            ObjectPlacement object;object.classId=shot.classId;object.position=shot.position;
            object.properties=scene_->classes[shot.classId.value].properties;
            spawnExplosions(object,shot.owner,propertyNumber(object.properties,"Altitude"),false,shot.nativeOwnerGeneration);
        }
        if(nativeRouting)syncNavigation(shot);
        return;
    }
    if((shot.visual && actors_[*shot.visual].nativeDamagePending) ||
       (shot.nativeBareBody && shot.nativeBareBody->damagePending)){
        // Native off/on handling precedes Energy death, which precedes
        // slope/propulsion. A later Ammo in this same projectile pass can
        // already have received lethal radial damage from an earlier one.
        if(!updateAmmoActivation(shot,seconds*1000)){if(nativeRouting)syncNavigation(shot);return;}
        if(shot.visual)destroy(*shot.visual,std::nullopt);
        else {
            ObjectPlacement object;object.classId=shot.classId;object.position=shot.position;
            object.properties=scene_->classes[shot.classId.value].properties;
            spawnExplosions(object,shot.owner<actors_.size()?std::optional<size_t>{shot.owner}:std::nullopt,propertyNumber(object.properties,"Altitude"),false,shot.nativeOwnerGeneration);
        }
        shot.alive=false;
        if(nativeRouting)syncNavigation(shot);
        return;
    }
    bool groundImpact=false;
    if(shot.slopeEnabled) {
        // 440e48: float accumulator plus sin(slope)*CURRENT INTEGER height.
        // Ground impact occurs before horizontal movement, not at range end.
        shot.slopeHeight=static_cast<float>(double(shot.slopeHeight)+std::sin(double(shot.slopeRadians))*shot.slopeAltitude);
        shot.slopeAltitude=std::trunc(shot.slopeHeight);
        if(shot.slopeAltitude<1){shot.slopeAltitude=0;groundImpact=true;}
        if(shot.visual)actors_[*shot.visual].projectileAltitude=shot.slopeAltitude;
    }
    if(!groundImpact && !updateAmmoActivation(shot,seconds*1000)){if(nativeRouting)syncNavigation(shot);return;}
    // 4406e0 detonates an expired Damage-after fuse before propulsion or
    // collision, even if Tracker has lost its target. It is not Life-time.
    const bool fuseExpired=shot.fuseMs>0 && shot.damageRemainingMs<=0;
    if(!groundImpact && !fuseExpired)updateProjectileGuidance(shot,seconds*1000);
    const auto from=shot.position;
    const auto fromNative=spiderSurfaceToNative(from.x,from.y);
    const NativeNavPoint radialCenter=shot.visual && actors_[*shot.visual].nativeMotionHistory()
        ?actors_[*shot.visual].nativeMotionHistory()->center:NativeNavPoint{fromNative.x,fromNative.y};
    // 438b20 uses class Thrust.Max for thrust, actor Speed-max only as a
    // magnitude gate. Derive this constant for player Rocket/Missile and
    // all Pulsar variants without changing the saved config layout.
    const auto& motionClass=scene_->classes[shot.classId.value];
    const bool nativePlayerLaunch=shot.owner==player_ &&
        (contentNameIs(motionClass.name,"missile-ammo-vz") || contentNameIs(motionClass.name,"rocket-ammo"));
    const float thrustMaximum=nativePlayerLaunch || nativePulsarAmmo(motionClass)?
        propertyNumber(children(motionClass.properties,"Thrust"),"Max",shot.motionConfig.speedMax):-1;
    const auto delta=groundImpact || fuseExpired?ProjectileDisplacement{}:advanceProjectileMotion(shot.motionConfig,shot.motion,shot.directionX,shot.directionZ,seconds*1000,thrustMaximum);
    if(shot.verticalBounce && !groundImpact && !fuseExpired) {
        const float altitude=projectileBounceSignedAltitude(shot.motion.magnitude,shot.ageMs,shot.bouncePeriod);
        // 4415bf -> 42c130 compares the current cell's complete native mask
        // with 0x100, before this tick's displacement. A mixed wall/water
        // mask is not the native water-only cell predicate.
        bool water=false;
        if(from.x>=0 && from.y>=0 && from.x<static_cast<double>(level_->mapW)*64 &&
           from.y<static_cast<double>(level_->mapH)*32) {
            const auto x=static_cast<size_t>(from.x/64),y=static_cast<size_t>(from.y/32);
            const size_t cell=y*level_->mapW+x;
            water=cell<level_->collisionMask.size() && level_->collisionMask[cell]==0x100;
        }
        if(altitude<0 && water) {
            if(const auto* fx=findClass(*scene_,"Water-Splash")) {
                const auto id=spawn(fx->id,from);
                if(id<actors_.size()){actors_[id].solid=false;actors_[id].enemy=false;actors_[id].effect=true;}
            }
            shot.alive=false;
            if(shot.visual){actors_[*shot.visual].alive=false;releaseNativeActorIdentity(*shot.visual);synchronizeActorCell(*shot.visual);}
            if(nativeRouting)syncNavigation(shot);
            return; // Native water disposal bypasses impact/explosion.
        }
        if(shot.visual)actors_[*shot.visual].projectileAltitude=std::abs(altitude);
    }
    shot.velocity={(delta.x-delta.z)*.8944272f/seconds,(delta.x+delta.z)*.4472136f/seconds};
    ContentPosition to{from.x+shot.velocity.x*seconds,from.y+shot.velocity.y*seconds};
    const auto nativeGrenade=!groundImpact && !fuseExpired?updateNativeGrenadeContacts(shot,delta,seconds):GrenadeNativeContactResult{};
    if(nativeGrenade.used){
        to={nativeGrenade.proposedX,nativeGrenade.proposedY};
        if(!shot.alive){if(nativeRouting)syncNavigation(shot);return;}
    }
    // 441884..441931 ->442ae9: moving nonplayer actors outside the
    // surface bounds are released before contacts/CREATE, without EXPL.
    // Fuse/ground termination takes the earlier440ebe path instead.
    if(!nativeGrenade.used && !groundImpact && !fuseExpired && (delta.x!=0 || delta.z!=0) &&
       (to.x<1 || to.y<1 || to.x>=level_->mapW*64.f || to.y>=level_->mapH*32.f)) {
        shot.alive=false;
        if(shot.visual){actors_[*shot.visual].alive=false;actors_[*shot.visual].solid=false;releaseNativeActorIdentity(*shot.visual);synchronizeActorCell(*shot.visual);}
        if(nativeRouting)syncNavigation(shot);
        return;
    }
    shot.distance+=std::sqrt(nativeDistanceSquared(to.x-from.x,to.y-from.y));
    //441b41: Over-water is an Ammo body flag too, not just vehicle movement.
    // Keep water shorelines separate from ordinary wall geometry. The grenade
    // native-selected-contact path applies the same flag independently.
    const bool overWater=motionClass.flags.overWater?*motionClass.flags.overWater:
        propertyBool(motionClass.properties,"Over-water");
    const auto projectileRoute=[&](ContentPosition end) {
        return terrainMoveAllowed(*level_,from.x,from.y,end.x,end.y,shot.radius,overWater);
    };
    float nearest=1;std::optional<size_t> target=nativeGrenade.target;
    bool terrain=nativeGrenade.terminal&&!target;
    if (!nativeGrenade.used && !groundImpact && !fuseExpired && !projectileRoute(to)) {
        float low=0,high=1;
        for (int step=0;step<16;++step) { const float mid=(low+high)/2;
            if (projectileRoute({from.x+(to.x-from.x)*mid,from.y+(to.y-from.y)*mid})) low=mid; else high=mid; }
        nearest=high;terrain=true;
    }
    if (!nativeGrenade.used && !groundImpact && !fuseExpired && ammoContactEnabled(shot)) {
        const auto check=[&](size_t i) {
            if (!actors_[i].alive || !actors_[i].solid || (shot.visual && i==*shot.visual) ||
                (i==shot.owner && (shot.ignoreOwner || shot.ageMs<500))) return;
            const auto q=scene_->objects[i].position;
            const auto hit=sweptCircleContact(from.x,from.y,to.x,to.y,q.x,q.y,shot.radius+actors_[i].radius);
            // Spatial iteration differs from ID order. Preserve the original
            // lowest-ID winner for exact ties, and terrain's priority on ties.
            if (hit && (*hit<nearest || (*hit==nearest && target && i<*target))) {
                nearest=*hit;target=i;terrain=false;
            }
        };
        size_t unindexed=0;
        if(actorBoundsActive_) {
            const double rx=double(shot.radius)*1.266,ry=double(shot.radius)*.633;
            const ActorBounds query{std::min(from.x,to.x)-rx,std::min(from.y,to.y)-ry,
                std::max(from.x,to.x)+rx,std::max(from.y,to.y)+ry};
            const auto overlaps=[&](const ActorBounds& b) {
                return !(query.maxX<b.minX || query.minX>b.maxX || query.maxY<b.minY || query.minY>b.maxY);
            };
            for(size_t g=0;g<actorBoundGroups_.size();++g)if(overlaps(actorBoundGroups_[g]))
                for(size_t n=g*16;n<std::min((g+1)*16,actorBoundOrder_.size());++n) {
                    const auto id=actorBoundOrder_[n];if(overlaps(actorBounds_[id]))check(id);
                }
            unindexed=actorBoundsIndexedCount_;
        }
        for(size_t i=unindexed;i<actors_.size();++i)check(i);
    }
    if(!nativeGrenade.used)shot.position={from.x+(to.x-from.x)*nearest,from.y+(to.y-from.y)*nearest};
    if(terrain && !target && shot.terrainBounce) {
        TerrainContacts contacts;
        const auto moved=moveOnTerrain(*level_,from.x,from.y,to.x-from.x,to.y-from.y,shot.radius,&contacts,false,overWater);
        if(contacts.count) {
            const auto& ammoClass=scene_->classes[shot.classId.value];
            const bool nativeSaw=contentNameIs(ammoClass.name,"saw-ammo-empty") ||
                                 contentNameIs(ammoClass.name,"saw-ammo") ||
                                 contentNameIs(ammoClass.name,"saw-ammo-vz");
            // 4449f0 rolls a grenade back before its physical response.
            shot.position=shot.verticalBounce || nativeSaw?from:ContentPosition{from.x+moved.x,from.y+moved.y};
            ContactVelocity velocity{shot.motion.velocityX,shot.motion.velocityZ};
            const float impact=shot.motion.magnitude*shot.motionConfig.weight*.1f;
            if(impact!=0 && std::abs(delta.x)+std::abs(delta.z)>2) {
                const auto& contact=contacts.values[0];
                const auto point=contactNativePoint(contact.x,contact.y);
                spawnProjectileBounceEffect(shot,from,{static_cast<float>(point.x),static_cast<float>(point.z)});
            }
            if(shot.verticalBounce || nativeSaw) {
                // 4367d0: float-stored angle/cosine response, with a
                // single selected terrain contact. Arc magnitude is not
                // refreshed until the next propulsion step (438b20).
                const auto& contact=contacts.values[0];
                velocity=resolveNativeTerrainContact(velocity,{contact.normalX,contact.normalZ},
                    shot.motion.thrust,shot.bounceElasticity,false).velocity;
            } else for(size_t i=0;i<contacts.count;++i)
                velocity=reflectTerrainVelocity(velocity,{contacts.values[i].normalX,contacts.values[i].normalZ},shot.bounceElasticity);
            shot.motion.velocityX=velocity.x;shot.motion.velocityZ=velocity.z;
            if(!shot.verticalBounce && !nativeSaw)shot.motion.magnitude=std::hypot(velocity.x,velocity.z);
            if(velocity.x!=0 || velocity.z!=0) {
                const auto projected=nativeToSurface(velocity.x,velocity.z);
                shot.velocity={projected.x/.014f,projected.y/.014f};
                const int direction=shot.verticalBounce || nativeSaw?spiderContactHeading(velocity):
                    nativeDirectionFromVector(projected.x,projected.y);
                const auto heading=nativeHeading(direction);const float length=std::hypot(heading.x,heading.y);
                shot.directionX=heading.x/length;shot.directionZ=heading.y/length;
                if(shot.visual)actors_[*shot.visual].direction=direction;
            }
            //4406e0/4367d0 retain the ammo and its fuse after a wall rebound.
            //The generic wall impact reduces shield without energy spill.
            if(shot.visual && std::abs(delta.x)+std::abs(delta.z)>2)
                actors_[*shot.visual].vitals.shield=std::max(0.f,actors_[*shot.visual].vitals.shield-impact);
            terrain=false;
        }
    }
    updateProjectileCreates(shot,from);
    if(target && !nativeGrenade.used) {
        // 43d7d0 moves Ammo to 4069d0's contact coordinate before its
        // radial hit and removal. Keep the accepted swept selection; only
        // use the traced narrow-phase point after the first actor is known.
        const auto q=scene_->objects[*target].position;
        const auto point=nativeAmmoImpactPoint(contactNativePoint(shot.position.x,shot.position.y),
            contactNativePoint(q.x,q.y),actors_[*target].radius);
        shot.position={static_cast<float>((point.x-point.z)*.8944271909999159),
                       static_cast<float>((point.x+point.z)*.4472135954999579)};
    }
    if (groundImpact || terrain || target || (shot.fuseMs>0 && shot.damageRemainingMs<=0)) {
        if(shot.visual){scene_->objects[*shot.visual].position=shot.position;synchronizeActorCell(*shot.visual);}
        const auto finitePosition=shot.position;
        if(nativeGrenade.impactSurface)shot.position=*nativeGrenade.impactSurface;
        applyAmmoImpact(shot,target,terrain || target.has_value(),radialCenter,seconds*1000.f);
        shot.position=finitePosition;
        shot.alive=false;
    }
    if (shot.visual) {
        scene_->objects[*shot.visual].position=shot.position;
        synchronizeActorCell(*shot.visual);
        if(!shot.alive)destroy(*shot.visual,shot.owner);
    } else if(!shot.alive) {
        ObjectPlacement object;object.classId=shot.classId;object.position=shot.position;
        object.properties=scene_->classes[shot.classId.value].properties;
        spawnExplosions(object,shot.owner,propertyNumber(object.properties,"Altitude"),false,shot.nativeOwnerGeneration);
    }
    if(shot.alive) {
        if(shot.nativeBareBody){
            const auto previous=spiderNativeToSurface(shot.nativeBareBody->motion.center.x,shot.nativeBareBody->motion.center.y);
            shot.nativeBareBody->motion.previousSurface={previous.x,previous.y};
            const auto current=spiderSurfaceToNative(shot.position.x,shot.position.y);
            shot.nativeBareBody->motion.center={current.x,current.y};
        }
        synchronizeBareAmmoCell(shot);
    }
    if(nativeRouting)syncNavigation(shot);
}
void GameWorld::updateProjectiles(float seconds) {
    const bool nativeRouting=std::any_of(projectiles_.begin(),projectiles_.end(),[](const auto& p){return p.guidanceClass.has_value();});
    for(size_t index=0;index<projectiles_.size();++index)updateProjectile(index,seconds,nativeRouting);
    for(auto& shot:projectiles_) {
        if(!nativeRouting)updateSpiderProjectileNavigation(shot);
        if(!shot.alive){clearProjectileNavigation(shot);releaseNativeProjectile(shot);}
    }
    std::erase_if(projectiles_,[](const auto& p){return !p.alive;});
    rebuildBareAmmoIndex();
}
void GameWorld::updateLifts(float milliseconds) {
    for(size_t i=0;i<actors_.size();++i)updateLift(i,milliseconds);
}
void GameWorld::updateLift(size_t i,float milliseconds) {
    if(transition_)return;
        auto& actor=actors_[i];if(!actor.alive || !actor.lift || !(actor.liftState.flags&LiftActive))return;
        if(actor.liftState.remainingMs==actor.lift->delayMs && !actor.gateAnimationFinished)return;
        const auto& object=scene_->objects[i];const auto& p=attribute(object.properties,"lift")->children;
        const auto position=object.position;
        std::optional<size_t> authorized;
        // 443430 traverses rows, then columns. Scene order within a cell is
        // the port's retained actor ordering (native linked-list order bounded).
        int bestY=0,bestX=0;
        for(size_t j=0;actor.liftState.keyId && (actor.liftState.flags&(LiftPresent|LiftGoingUp)) && j<actors_.size();++j) {
            const auto& other=actors_[j];if(!other.alive || (j!=player_&&!other.enemy))continue;
            if(scene_->objects[j].flags.invisible.value_or(false))continue;
            const auto q=scene_->objects[j].position;
            const int x=static_cast<int>(q.x/64),y=static_cast<int>(q.y/32);
            if(std::abs(x-static_cast<int>(position.x/64))>2 || std::abs(y-static_cast<int>(position.y/32))>2)continue;
            if(actor.liftState.keyId && !hasKey(other.heldKeys,actor.liftState.keyId) && !other.keys.contains(actor.liftState.keyId))continue;
            if(!authorized || y<bestY || (y==bestY&&x<bestX)){authorized=j;bestY=y;bestX=x;}
        }
        bool occupied=false;
        const int usage=static_cast<int>(propertyNumber(p,"Usage-Count"));
        for(size_t j=0;(actor.liftState.flags&LiftPresent) && milliseconds>=actor.liftState.remainingMs && usage>0 && j<actors_.size();++j) {
            const auto& other=actors_[j];if(j==i || !other.alive || !other.solid || other.effect)continue;
            if(other.pickup)continue;
            const auto q=scene_->objects[j].position;
            if(std::abs(static_cast<int>(position.x/64)-static_cast<int>(q.x/64))>1 ||
               std::abs(static_cast<int>(position.y/32)-static_cast<int>(q.y/32))>1)continue;
            const float dx=q.x-position.x,dy=q.y-position.y,r=actor.radius+other.radius;
            if(std::abs(.559016994f*dx+1.118033989f*dy)<r &&
               std::abs(-.559016994f*dx+1.118033989f*dy)<r){occupied=true;break;}
        }
        const auto result=advanceLift(*actor.lift,actor.liftState,
            {milliseconds,actor.gateAnimationFinished,authorized.has_value(),occupied,usage});
        actor.solid=!actor.liftState.passable;
        if(result.restartAnimation){++actor.gateRevision;actor.gateAnimationFinished=false;}
        if(result.consumeKeyId && authorized) {
            const bool code=(actor.liftState.flags&LiftNeedsCode)!=0;
            if(consumeHeldKey(*authorized,result.consumeKeyId) && *authorized==player_)
                notifications_.keyUsed(result.consumeKeyId,code);
        }
        if(!result.departureReady || !player().alive || rejectedLifts_.contains(i) ||
           scene_->objects[player_].flags.invisible.value_or(false))return;
        const auto& currentObject=scene_->objects[i];
        const auto& currentProperties=attribute(currentObject.properties,"lift")->children;
        const auto here=playerPosition();const float margin=actors_[i].radius-player().radius;
        const float dx=here.x-position.x,dy=here.y-position.y;
        if (margin<0 || std::abs(.559016994f*dx+1.118033989f*dy)>margin ||
            std::abs(-.559016994f*dx+1.118033989f*dy)>margin) return;
        const int64_t cost=std::max<int64_t>(0,-static_cast<int64_t>(propertyNumber(currentObject.properties,"Kill-Count")));
        if (player().vitals.money-(saveRequest_?saveRequest_->cost:0)<cost) { message_="Insufficient credits for Lift";return; }
        const auto destination=liftDestination(currentProperties,scene_->mapPath);if(!destination)return;
        transition_=TransitionRequest{i,*destination,cost};return;
}

void GameWorld::rejectTransition(const std::string& reason) {
    if (transition_ && transition_->lift<actors_.size() && actors_[transition_->lift].lift)
        rejectedLifts_.insert(transition_->lift);
    transition_.reset();message_="Cannot change level: "+reason;diagnostics_.push_back(message_);
}
void GameWorld::finishEnding() {
    if(!transition_ || !endingDestination(transition_->path))return;
    if(transition_->lift<scene_->objects.size())consumeLiftUse(scene_->objects[transition_->lift]);
    actors_[player_].vitals.money-=transition_->cost;
    transition_.reset();
    endingSource_.reset();endingRemainingMs_=0;
}
void GameWorld::beginFinalEnding(size_t source) {
    if(endingSource_)return;
    endingSource_=source;endingRemainingMs_=4500;
    // 43db70 ->43d980 clears eligible world actors, excluding the player and
    // Player-direct-fire-only objects. Temporary invulnerability is not tested.
    const size_t count=actors_.size();
    for(size_t id=0;id<count;++id) {
        if(id==player_ || !actors_[id].alive || actors_[id].indestructible ||
           propertyBool(scene_->objects[id].properties,"Player-direct-fire-only"))continue;
        actors_[id].vitals.energy=0;destroy(id,std::nullopt);
    }
}
PlayerCarry GameWorld::carry() const {
    const auto& p=player();auto ammunition=p.ammo;
    if(surprise_.active(SurpriseKind::Ammo))restoreAmmoBaseline(ammunition,surprise_.savedAmmo);
    auto held=heldItems_;std::erase_if(held,[&](const auto& item){return item.holder!=player_;});
    return {p.vitals,ammunition,p.keys,p.heldKeys,p.weapons.empty()?"":p.weapons[p.selectedWeapon].name,playerKills_,elapsedMs_,p.weapons.empty()?std::nullopt:p.weapons[p.selectedWeapon].ammoSlot,std::move(held),ammoHits_,ammoMisses_};
}
void GameWorld::restore(const PlayerCarry& carry) {
    audioInput_.reset();audioMotorAttempt_=false;playerMoveCommanded_=false;
    playerKills_=carry.kills;ammoHits_=carry.ammoHits;ammoMisses_=carry.ammoMisses;elapsedMs_=carry.elapsedMs;
    auto& p=actors_[player_];p.vitals=carry.vitals;p.ammo=carry.ammo;p.keys=carry.keys;p.heldKeys=carry.heldKeys;
    auto incomingHeld=carry.heldItems;
    if(surprise_.kind==SurpriseKind::None)std::erase_if(incomingHeld,[](const auto& held){return held.surprise;});
    releaseNativeHeldItemsForReplacement(incomingHeld);
    heldItems_=std::move(incomingHeld);for(auto& held:heldItems_)held.holder=player_;
    migrateHeldCodes();migrateHeldItems();adoptNativeHeldItems();
    for(auto& weapon:p.weapons)selectCompatibleAmmo(weapon,p,scene_->objects[player_],*scene_);
    for (size_t i=0;i<p.weapons.size();++i) if (folded(p.weapons[i].name)==folded(carry.weapon)) p.selectedWeapon=i;
    if(!p.weapons.empty() && carry.ammoSlot && *carry.ammoSlot<p.ammo.size()) {
        auto& weapon=p.weapons[p.selectedWeapon];
        if(p.ammo.slots[*carry.ammoSlot].first==weapon.ammo)weapon.ammoSlot=carry.ammoSlot;
    }
}
size_t GameWorld::enemiesRemaining() const { return std::count_if(actors_.begin(),actors_.end(),[](const auto& a){return a.alive&&a.hudEnemy;}); }
float GameWorld::playerSurfaceSpeed() const { const auto v=nativeToSurface(motion_.velocityX,motion_.velocityZ);return std::hypot(v.x,v.y)/.014f; }
void GameWorld::toggleMap() {
    if(!player().alive)return;
    if(!map_.toggle(player().vitals.money-(saveRequest_?saveRequest_->cost:0))) {
        notifications_.mapDenied(playerName_);
        voiceEvent("V-NEED-CASH.WAV",0,1000,5000);
        message_.clear();
    }
    else message_.clear();
}
bool GameWorld::actorUpdateAdmitted(size_t actor) const {
    return !actors_[actor].nativeCameraGated || effectPositionAdmitted(scene_->objects[actor].position);
}


bool GameWorld::advanceActorAI(size_t i,float seconds) {
    auto& a=actors_[i];bool playerLocked=false;
        const auto pos=scene_->objects[i].position,target=playerPosition();
        std::span<const ContentPosition> path;
        for (const auto& candidate:scene_->paths) if (candidate.id.value==a.brain.pathId) path=candidate.points;
        const auto subtype=objectSubtype(scene_->objects[i]);
        const bool spider=subtype=="spider",walker=subtype=="walker";
        if(spider)return advanceSpiderAI(i,seconds,path);
        if(a.ordinaryAIConfig.enabled)return advanceOrdinaryEnemyAI(i,seconds,path);
        if(a.enemyTurn) { // 440fe1: clear both commands before AI dispatch.
            a.enemyTurn->turnCommand=0;a.enemyTurn->gunCommand=0;
        }
        if(!a.nativeMotion){const auto n=spiderSurfaceToNative(pos.x,pos.y);a.nativeMotion=SpiderMotionHistory{{n.x,n.y},pos};}
        const int nativeAim=nativeDirectionFromVector(target.x-pos.x,target.y-pos.y);
        // 42ca30 admits Spider seeker only for a two-node, four-axis route.
        // Native direct actor-obstacle admission is centreline vs raw AABBs.
        // Terrain routing retains its existing conservative clearance adapter;
        // merely detecting a nearby player must not suspend its patrol.
        const bool seekerRouteAccepted=!spider || (nativeAim<24 && nativeAim%6==0 &&
            clearRoute(pos,target,a.radius) && !dynamicBlocked(i,pos,target,0,true,true));
        auto intent=advanceBrain(a.brain,a.brainState,{seconds*1000,pos,target,a.direction,
            scene_->objects[player_].classId,player().alive,!scene_->objects[player_].flags.invisible.value_or(false),
            clearRoute(pos,target,1),a.weapons.empty()?0:a.weapons[a.selectedWeapon].range,path,a.reached,seekerRouteAccepted,spider,spider,walker});
        a.reached=false;
        a.routeRetryMs-=seconds*1000;
        const bool seekerActive=a.brainState.acquired && seekerRouteAccepted;
        playerLocked|=seekerActive && (a.brain.seeker || a.brain.tracker);
        a.spiderPatrolTurning=false; // Retired v23 proximity-turn state.

        if (seekerActive && !spider && (a.brain.base!=BrainBase::Fixed || walker) &&
            nativeDistanceSquared(pos.x-target.x,pos.y-target.y)>a.brain.minimumDistance*a.brain.minimumDistance)
            intent.destination=target;
        if (!seekerActive && a.brain.base==BrainBase::Random && !intent.destination) {
            a.randomWaitMs-=seconds*1000;
            if (a.randomGoal && nativeDistanceSquared(pos.x-a.randomGoal->x,pos.y-a.randomGoal->y)<4) {
                a.randomGoal.reset();a.randomWaitMs=a.brain.randomDelayMs;
            }
            if (!a.randomGoal && a.randomWaitMs<=0) {
                a.randomGoal=chooseRandomDestination(pos,a.brain.randomRange,level_->mapW,level_->mapH,
                    [&](int low,int high){ return std::uniform_int_distribution<int>(low,high)(random_); },
                    [&](int x,int y){
                        const ContentPosition center{x*64.f+32,y*32.f+16};
                        if (!terrainWalkable(*level_,center.x,center.y)) return false;
                        for (size_t other=0;other<actors_.size();++other) if (other!=i && actors_[other].alive && actors_[other].solid) {
                            const auto q=scene_->objects[other].position;
                            if (static_cast<int>(q.x/64)==x && static_cast<int>(q.y/32)==y) return false;
                        }
                        return true;
                    });
                if (!a.randomGoal) a.randomWaitMs=a.brain.randomDelayMs;
            }
            if (a.randomGoal) intent.destination=intent.aim=a.randomGoal;
        }
        // Preserve bounded route search, but feed route commands into the native
        // propulsion solver instead of replacing momentum with a constant step.
        std::optional<ContentPosition> destination=intent.destination;
        if(destination) {
            if (!clearRoute(pos,*destination,a.radius) || dynamicBlocked(i,pos,*destination,a.radius,seekerActive,spider)) {
                const bool changed=!a.routeGoal || nativeDistanceSquared(*a.routeGoal,*destination)>1024;
                if (a.routeRetryMs<=0 && (changed || a.routePoint>=a.route.size())) {
                    if (!a.routeSearch || changed) {
                        a.routeSearch.emplace(*level_,pos,*destination,a.radius,2048);
                        a.routeGoal=destination;
                    }
                }
                while(a.routePoint<a.route.size() && nativeDistanceSquared(pos,a.route[a.routePoint])<=64)++a.routePoint;
                if(a.routePoint<a.route.size())destination=a.route[a.routePoint];else destination.reset();
            } else {a.route.clear();a.routePoint=0;a.routeGoal.reset();a.routeSearch.reset();}
        }
        const bool smallFamily=spider || walker || subtype=="tank";
        if(destination && (!a.driveSegmentEnd || nativeDistanceSquared(*a.driveSegmentEnd,*destination)>.0001f)) {
            a.driveSegmentStart=pos;a.driveSegmentEnd=destination;
        }
        const auto aim=destination ? destination : intent.aim;
        const bool canTurn=propertyBool(scene_->objects[i].properties,"Can-turn-on-place");
        const float targetDistance=nativeDistanceSquared(pos,target),minimum2=a.brain.minimumDistance*a.brain.minimumDistance;
        // Direct terminal target-facing is action mask4. Generic route/search
        // admission retains its separately documented adapter.
        const bool targetFacing=seekerActive && clearRoute(pos,target,a.radius) && targetDistance<=minimum2;
        int desired=24;
        if (aim && (canTurn || a.enemyMagnitude>=1)) {
            const auto origin=destination && a.driveSegmentStart?*a.driveSegmentStart:pos;
            const auto from=spiderSurfaceToNative(origin.x,origin.y),to=spiderSurfaceToNative(aim->x,aim->y);
            desired=spiderContactHeading({to.x-from.x,to.y-from.y});
            if(spider && desired<24)desired=((desired+3)/6*6)%24;
            const float delay=std::max(1.f,propertyNumber(scene_->objects[i].properties,"Turn-timeout",42));
            if(spider)advanceSpiderBodyPose(a.direction,a.turnMs,desired,seconds*1000,delay);
            else if(a.enemyTurn && desired<24) {
                const int ms=static_cast<int>(std::floor(elapsedMs_)-std::floor(elapsedMs_-seconds*1000));
                advanceEnemyBodyTurn(a.enemyTurnConfig,*a.enemyTurn,desired,targetFacing,ms,a.enemyMagnitude,a.speed);
                advanceEnemyGunTurn(a.enemyTurnConfig,*a.enemyTurn,desired,targetFacing,ms);
                synchronizeEnemyGunTurnHeading(a.enemyTurnConfig,*a.enemyTurn);
                a.direction=a.enemyTurn->logicalHeading;
            } else {
                a.turnMs+=seconds*1000;
                while(desired<24 && a.direction!=desired && a.turnMs>=delay) {
                    const int delta=(desired-a.direction+24)%24;
                    a.direction=(a.direction+(delta<=12?1:23))%24;a.turnMs-=delay;
                }
                if(a.direction==desired)a.turnMs=0;
            }
        }
        EnemyMotionCommand command;command.heading=spider?(a.direction/6)*6:a.direction;
        if(destination && (!spider || a.direction%6==0))command.throttle=EnemyThrottle::Forward;
        const int contactAim=nativeDirectionFromVector(target.x-pos.x,target.y-pos.y);
        const int headingGap=contactAim<24?std::abs(contactAim-a.direction):24;
        const bool nearTarget=seekerActive && clearRoute(pos,target,a.radius) &&
            (canTurn || (contactAim<24 && std::min(headingGap,24-headingGap)<=1));
        bool guidedAmmo=false;
        if(!a.weapons.empty())if(const auto* ammo=findClass(*scene_,a.weapons[a.selectedWeapon].ammo))
            guidedAmmo=std::any_of(ammo->behaviours.begin(),ammo->behaviours.end(),
                [](const auto& b){return contentNameIs(b,"tracker");});
        auto firing=seekerActive && !a.weapons.empty()
            ?enemyFireAim(pos,target,desired,a.weaponHeading(),a.brain.minimumDistance,
                a.weapons[a.selectedWeapon].range,nearTarget,guidedAmmo):EnemyFireAim{};
        if(a.enemyTurn && a.enemyTurn->turnCommand)firing.fire=false;
        // 42d2d0 clears the retained range bit only when called for the terminal
        // target-facing action with a nonnull target. Route/idle passes retain
        // it; the idle gun controller selects its period from this saved bit.
        if(a.enemyTurn && a.enemyTurnConfig.twoPieces && targetFacing)
            a.enemyTurn->gunInRange=firing.inRange;
        if(nearTarget && targetDistance<=minimum2) {
            command.throttle=EnemyThrottle::Neutral;
            if(targetDistance<=.25f*minimum2) {
                a.contactMotion=false;a.enemyThrust=std::min(a.enemyThrust,0.f);
                command.throttle=EnemyThrottle::Reverse;
            }
        } else if(destination && intent.destination && !seekerActive &&
                  a.brain.base==BrainBase::Patrol &&
                  nativeDistanceSquared(pos,*intent.destination)<=std::pow(spider?4.f:walker?5.f:64.f,2)) {
            command.throttle=EnemyThrottle::Neutral;
            a.reached=intent.patrolDestination;
        }
        if(!seekerActive && a.driveSegmentStart && a.driveSegmentEnd) {
            command.route=a.brain.base==BrainBase::Random?EnemyRouteMotion::Random:
                a.brain.base==BrainBase::Patrol?EnemyRouteMotion::Patrol:EnemyRouteMotion::None;
            const auto n=surfaceToNative(pos.x,pos.y),from=surfaceToNative(a.driveSegmentStart->x,a.driveSegmentStart->y),to=surfaceToNative(a.driveSegmentEnd->x,a.driveSegmentEnd->y);
            command.routeCorrection=enemyRouteCorrection({n.x,n.y},{from.x,from.y},{to.x,to.y},smallFamily);
        }
        a.enemyMoveCommand=command.throttle==EnemyThrottle::Forward?200:
            command.throttle==EnemyThrottle::Reverse?208:0;
        // 4412fe ->444a40: stationary targeting actors run their one-piece
        // body or multipart gun controller after seeker/base commands. Actual
        // position history and the cleared/final body command admit both.
        if(a.enemyTurn && (supportsMultipartEnemyIdle(a.enemyTurnConfig) ||
                          (a.enemyTurn->bodyPeriodMs>0 && supportsOnePieceEnemyIdle(a.enemyTurnConfig)))) {
            const auto actual=spiderSurfaceToNative(pos.x,pos.y);
            const auto previous=spiderSurfaceToNative(a.nativeMotion->previousSurface.x,a.nativeMotion->previousSurface.y);
            const EnemyIdleTurnAdmission admission{a.brain.seeker||a.brain.tracker,
                actual.x,actual.y,previous.x,previous.y,a.enemyMoveCommand};
            const bool onePiece=onePieceEnemyIdleTurnAdmitted(a.enemyTurnConfig,*a.enemyTurn,admission);
            const bool multipart=multipartEnemyIdleTurnAdmitted(a.enemyTurnConfig,*a.enemyTurn,admission);
            if(onePiece || multipart) {
                const int ms=static_cast<int>(std::floor(elapsedMs_)-std::floor(elapsedMs_-seconds*1000));
                const auto random=[&](int low,int high){return nativeWorldRandom_.inclusive(low,high);};
                if(onePiece)advanceOnePieceEnemyIdleTurn(a.enemyTurnConfig,*a.enemyTurn,ms,random);
                else advanceMultipartEnemyIdleTurn(a.enemyTurnConfig,*a.enemyTurn,ms,random);
                a.direction=a.enemyTurn->logicalHeading;command.heading=a.direction;
            }
        }
        advanceShieldRegeneration(i);
        a.walkingCommand=(walker && command.throttle!=EnemyThrottle::Neutral) ||
            (spider && command.throttle==EnemyThrottle::Forward && a.direction%6==0);
        EnemyMotionState motion{a.contactVelocity,a.enemyThrust,a.enemyMagnitude,a.contactMotion};
        const auto step=advanceEnemyMotion(motion,command,seconds,a.enemyMotionConfig);
        a.contactVelocity=motion.velocity;a.enemyThrust=motion.thrust;a.enemyMagnitude=motion.magnitude;a.contactMotion=motion.impactLatch;
        const float factor=14.f/(seconds*1000.f);
        const ContactVelocity nativeStep{motion.velocity.x/factor,motion.velocity.z/factor};
        a.contactVelocity={nativeStep.x*factor,nativeStep.z*factor};
        const auto rest=nativeActorRestDecision(i,nativeStep);
        if(rest.rested){a.enemyMovedLastStep=false;a.nativeMotion->previousSurface=pos;a.contactVelocity={};if(rest.sleep)sleepNativeActor(i);}
        const auto moved=rest.rested?TerrainDisplacement{}:move(i,step.x,step.y,true,&nativeStep);
        if(moved.ammoAbort)return playerLocked;
        if(smallFamily && !moved.constrained && !((step.x!=0 || step.y!=0) && lastSpiderSeparated_))a.contactVelocity={};
        if(walker)a.walkingCommand=a.enemyMoveCommand==200;
        a.locomoting=nativeDistanceSquared(pos,scene_->objects[i].position)>.0001f;
        if(moved.constrained){a.route.clear();a.routePoint=0;}
        a.enemyRevealed=firing.inRange && propertyBool(scene_->objects[i].properties,"Visible-when-firing");
        // Native reveal/spin-up follows the in-range return, even when actual
        // target-heading alignment declines a shot. Emit after movement as before.
        fire(i,firing.fire,false,{},firing.inRange); // may append actors
    return playerLocked;
}

void GameWorld::tick(float seconds,const WorldInput& suppliedInput) {
    audioInput_.reset();audioMotorAttempt_=false;playerMoveCommanded_=false;
    if (!(seconds>0) || !std::isfinite(seconds) || seconds>0.1f || !scene_) return;
    currentTickMilliseconds_=seconds*1000.f;
    const WorldInput input=endingSource_?WorldInput{}:suppliedInput;
    audioMotorAttempt_=input.movement.forward>0;
    playerMoveCommanded_=input.movement.forward!=0;
    if(endingSource_ && endingRemainingMs_>0) {
        endingRemainingMs_=std::max(0.0,endingRemainingMs_-seconds*1000.0);
        if(endingRemainingMs_==0)transition_=TransitionRequest{*endingSource_,"*",0};
    }
    // 43e5f0 expires the combo before combat, including Time Warp's skipped AI ticks.
    updateCombatBonus(seconds*1000);
    const bool worldTick=!surprise_.active(SurpriseKind::TimeWarp) || surprise_.timeWarpPhase==0;
    // 41c180 creates independently Managed FX11 children; 444ca0/4452c0
    // advance their timers and parent immunity even when that parent sleeps.
    // The port collapses the synchronized15 Final-PC children into one timer.
    // Only full manager passes advance them (not Time Warp player-only passes).
    if(worldTick)for(const auto id:attachedProtectionActors_)
        if(id<actors_.size()&&actors_[id].alive)actors_[id].timedProtection.advance(seconds*1000);
    if(surprise_.active(SurpriseKind::TimeWarp))surprise_.timeWarpPhase=(surprise_.timeWarpPhase+1)%4;
    const float tripleTarget=surprise_.active(SurpriseKind::Triple)&&input.fire?400.f:0.f;
    if(tripleTarget>0 && surprise_.tripleOffset==0)surprise_.tripleOffset=100;
    surprise_.tripleOffset=std::clamp(surprise_.tripleOffset+(tripleTarget>0?20.f:-50.f)*seconds/.014f,0.f,400.f);
    actorBounds_.resize(actors_.size());actorBoundsIndexedCount_=actors_.size();
    for(size_t i=0;i<actors_.size();++i) {
        actors_[i].nativeAmmoAbort=false;
        updateActorBounds(i);

    }
    actorBoundOrder_.clear();actorBoundGroup_.assign(actors_.size(),std::numeric_limits<size_t>::max());
    for(size_t i=0;i<actors_.size();++i)
        if((actors_[i].alive || actors_[i].reactivateMs>=0) &&
           ((!actors_[i].effect && !actors_[i].pooledTrail) || actors_[i].solid))actorBoundOrder_.push_back(i);
    std::sort(actorBoundOrder_.begin(),actorBoundOrder_.end(),[&](size_t a,size_t b){
        return actorBounds_[a].minX<actorBounds_[b].minX;
    });
    actorBoundGroups_.clear();
    for(size_t n=0;n<actorBoundOrder_.size();++n) {
        const auto i=actorBoundOrder_[n];const auto& b=actorBounds_[i];
        actorBoundGroup_[i]=n/16;
        if(n%16==0)actorBoundGroups_.push_back(b);
        else {
            auto& group=actorBoundGroups_.back();
            group.minX=std::min(group.minX,b.minX);group.minY=std::min(group.minY,b.minY);
            group.maxX=std::max(group.maxX,b.maxX);group.maxY=std::max(group.maxY,b.maxY);
        }
    }
    actorBoundsActive_=true;
    struct BoundsScope { bool& active; ~BoundsScope(){active=false;} } boundsScope{actorBoundsActive_};
    elapsedMs_+=seconds*1000;
    actors_[player_].shieldPulse=surprise_.active(SurpriseKind::Shield)?shieldRegenPulse(elapsedMs_):0;
    notifications_.advance(seconds*1000.0);
    if(player().alive && player().vitals.shieldMax>0 && player().vitals.shield/player().vitals.shieldMax<=.01f) {
        notifications_.greenInfo(NoticeKind::Warning,notifications_.language()==2?"DANGER : BOUCLIER FAIBLE":"DANGER : SHIELD IS LOW",100,100,500,true);
        voiceEvent("V-SHIELD-LOW.WAV",0,1000,15000);
    }
    for(auto& actor:actors_)actor.hitFlashMs=std::max(0.f,actor.hitFlashMs-seconds*1000);
    auto& feedback=actors_[player_];
    const auto& visualProperties=scene_->objects[player_].properties;
    const auto& thrustLimits=children(visualProperties,"Thrust");
    const float acceleration=std::max(0.f,propertyNumber(visualProperties,"Acceleration-step",1));
    const float deceleration=std::max(0.f,propertyNumber(visualProperties,"Decceleration-step",.5f));
    // 4406e0/438b20 presentation drive: signed thrust and a one-second forward
    // latch. Keep presentation separate from the persisted motion state.
    if(feedback.alive && input.movement.forward>0) {
        feedback.engineGlowMs=1000;
        feedback.visualThrust=std::min(propertyNumber(thrustLimits,"Max",45),feedback.visualThrust+acceleration);
    } else {
        feedback.engineGlowMs=std::max(0.f,feedback.engineGlowMs-seconds*1000);
        if(feedback.alive && input.movement.forward<0)
            feedback.visualThrust=std::max(propertyNumber(thrustLimits,"Min",-10),feedback.visualThrust-deceleration*3);
        else if(feedback.visualThrust>deceleration)feedback.visualThrust-=deceleration;
        else if(feedback.visualThrust< -deceleration)feedback.visualThrust+=deceleration;
        else feedback.visualThrust=0;
    }
    // Fair shared budget: route requests persist across ticks instead of each
    // enemy running an entire 2048-node search in one frame.
    const auto count=actors_.size();
    //440703 clears world+20 bit200000 before either manager mode. AI target
    //actions rebuild it; a player-only Time Warp pass therefore leaves it off.
    playerLocked_=false;
    bool& playerLocked=playerLocked_;
    size_t navigationBudget=worldTick && debugEnemyAI_?64:0;
    for(size_t visited=0;visited<count && navigationBudget; ++visited) {
        const size_t id=navigationCursor_++%count;
        auto& actor=actors_[id];
        if(!actor.alive || !actor.routeSearch || !actorUpdateAdmitted(id))continue;
        constexpr size_t slice=16;
        const bool ignorePlayer=actor.brainState.acquired && objectSubtype(scene_->objects[id])!="spider";
        auto route=actor.routeSearch->advance(*level_,
            [&](ContentPosition from,ContentPosition to,float radius){return dynamicBlocked(id,from,to,radius,ignorePlayer,objectSubtype(scene_->objects[id])=="spider");},slice);
        navigationBudget-=slice;
        if(route.status!=RouteStatus::Pending) {
            actor.route=std::move(route.points);actor.routePoint=0;
            actor.routeRetryMs=500;actor.routeSearch.reset();
        }
    }
    const auto valid=[&](std::uint64_t body) {
        if(body&NativeAwakeAmmoBit) {
            const auto at=bareAmmoIndex_.find(body&~NativeAwakeAmmoBit);
            return at!=bareAmmoIndex_.end() && projectiles_[at->second].nativeGeneration!=0;
        }
        return body<actors_.size() && actors_[body].nativeGeneration!=0;
    };
    const auto moved=[&](std::uint64_t body,size_t index) {
        if(body&NativeAwakeAmmoBit) {
            const auto at=bareAmmoIndex_.find(body&~NativeAwakeAmmoBit);
            if(at!=bareAmmoIndex_.end())projectiles_[at->second].nativeBareBody->awakeIndex=index;
        } else if(body<actors_.size())actors_[body].nativeAwakeIndex=index;
    };
    const auto visit=[&](std::uint64_t body) {
        if(body&NativeAwakeAmmoBit) {
            const auto at=bareAmmoIndex_.find(body&~NativeAwakeAmmoBit);
            if(at!=bareAmmoIndex_.end())updateProjectile(at->second,seconds,true);
        } else visitNativeActor(body,seconds,input,playerLocked);
        //Later bodies see contact/death/factory effects from this visit now.
        auto impacts=std::move(contactDamage_);contactDamage_.clear();
        for(const auto& request:impacts)applyNativeContactDamage(request.target,request.value);
        resolveAmmoContacts();
        auto touch=std::move(touchVictims_);touchVictims_.clear();
        for(auto id:touch)destroy(id,player_);
        //Physical Ammo release precedes the next body. Compact storage is
        //erased only after traversal; stable identities survive vector moves.
        if(body&NativeAwakeAmmoBit) {
            const auto at=bareAmmoIndex_.find(body&~NativeAwakeAmmoBit);
            if(at!=bareAmmoIndex_.end() && !projectiles_[at->second].alive) {
                auto& shot=projectiles_[at->second];clearProjectileNavigation(shot);releaseNativeProjectile(shot);
            }
        }
        if(body==player_)collect();
    };
    if(worldTick) {
        size_t cursor=0;
        while(auto body=awakeManager_.next(cursor,valid,moved)) {
            awakeVisitIndex_=cursor-1;visit(*body);
        }
    } else if(player_<actors_.size() && actors_[player_].nativeGeneration) {
        //43f6d7 chooses mode1 instead of a full manager pass.
        awakeVisitIndex_=awakeManager_.entries.size();visit(player_);
    }
    awakeVisitIndex_.reset();deferActorFire_=false;deferredActorFire_.reset();
    for(auto& shot:projectiles_) {
        if(shot.visual && *shot.visual<actors_.size() && !actors_[*shot.visual].alive && actors_[*shot.visual].reactivateMs<0)shot.alive=false;
        if(!shot.alive){clearProjectileNavigation(shot);releaseNativeProjectile(shot);}
    }
    std::erase_if(projectiles_,[](const auto& shot){return !shot.alive;});
    rebuildBareAmmoIndex();

    if(playerLocked) {
        notifications_.greenInfo(NoticeKind::Warning,notifications_.language()==2?"ATTENTION : VOUS ETES REPERE":"WARNING : YOU ARE LOCKED",100,90,500,true);
        static const char* voices[]={"V-WARNING.WAV","V-WATCH-OUT.WAV","V-TAKE-CARE.WAV","V-LOCKED.WAV"};
        voiceEvent(voices[static_cast<uint64_t>(elapsedMs_)&3],0,1000,10000);
    }
    updateSurpriseOrbitPositions();
    updateSaving(seconds*1000);
    //435000 economics is independent of player awake/Circular/Time Warp.
    // Keep the documented stable60Hz adapter, never bill from rendering.
    if(map_.open() && player().alive) {
        auto& money=actors_[player_].vitals.money;
        const auto before=money;
        money-=map_.advance(seconds*1000.0,money-(saveRequest_?saveRequest_->cost:0));
        if(before/5000!=money/5000)audioEvent(AudioEventKind::MapCash,player_);
        if(!map_.open()) {
            notifications_.mapDenied(playerName_);
            voiceEvent("V-NEED-CASH.WAV",0,1000,5000);
        }
    }

}
}
