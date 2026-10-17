#include "game/world.h"
#include "game/properties.h"
#include "game/projectile_contact_native.h"
#include "game/native_impact_coordinate.h"
#include <cmath>

namespace vyruz {
void GameWorld::spawnProjectileBounceEffect(const Projectile& shot,ContentPosition source,ContentPosition nativePoint) {
    //43d140: source CLASS Spark-on-hit-point, unlike direct Hit-fx's rules.
    const auto& properties=scene_->classes[shot.classId.value].properties;
    //43d163: Player/Surprise8 bypasses the ordinary class Spark flag.
    //A transient source view from actor movement shares this producer with Ammo.
    const bool pinball=shot.visual==player_ && surprise_.active(SurpriseKind::Pinball);
    if((!pinball && !propertyBool(properties,"Spark-on-hit-point")) || !effectPositionAdmitted(source))return;
    const auto point=nativeBounceFxPoint(source,nativePoint);
    //40a010 assigns Bounce-fx to all four slots; directional assignments
    //subsequently replace just one slot. Respect resolved property order.
    std::string name;
    constexpr std::string_view directional[]={"bounce-fx-up-left","bounce-fx-up-right",
                                              "bounce-fx-down-left","bounce-fx-down-right"};
    for(const auto& node:properties) {
        if(contentNameIs(node.key,"bounce-fx") || contentNameIs(node.key,directional[point.quadrant]))
            name=node.values.empty()?"":node.values.front();
    }
    constexpr std::string_view flipper[]={"Flipper-Bong-Up-Left","Flipper-Bong-Up-Right",
                                         "Flipper-Bong-Down-Left","Flipper-Bong-Down-Right"};
    const auto* fx=pinball?findClass(*scene_,std::string(flipper[point.quadrant])):nullptr;
    //43d22c falls back to authored directional Bounce-fx if the special class
    //is absent; native Pinball does not rewrite the player's class properties.
    if(!fx)fx=findClass(*scene_,name);
    if(!fx)return;
    const auto position=point.placement;
    if(!std::isfinite(position.x)||!std::isfinite(position.y) || position.x<0 || position.y<0 ||
       position.x>=level_->mapW*64.f || position.y>=level_->mapH*32.f)return;
    const int cellX=static_cast<int>(position.x)>>6,cellY=static_cast<int>(position.y)>>5;
    //43d2c7 checks ANY instance of the class in this cell, not Uniq-at-xy or
    //exact position. Native cell-list duplicates/membership remain separate.
    if(pinball) {
        const auto cell=actorCells_.coordinate(position.x,position.y);
        if(cell!=NativeActorCells::Missing)
            for(size_t id=actorCells_.heads[cell];id!=NativeActorCells::Missing;id=actorCells_.next[id])
                if(scene_->objects[id].classId==fx->id)return;
    } else for(size_t i=0;i<actors_.size();++i)
        if((actors_[i].alive || actors_[i].reactivateMs>=0) && scene_->objects[i].classId==fx->id &&
           (static_cast<int>(scene_->objects[i].position.x)>>6)==cellX &&
           (static_cast<int>(scene_->objects[i].position.y)>>5)==cellY)return;
    const auto classId=fx->id;
    const bool ground=fx->flags.inGround.value_or(false);
    const bool effect=attribute(fx->properties,"fx")!=nullptr;
    const float altitude=std::trunc(shot.visual && actors_[*shot.visual].projectileAltitude
        ?*actors_[*shot.visual].projectileAltitude:shot.slopeAltitude);
    const auto id=spawn(classId,position);if(id>=actors_.size())return;
    scene_->objects[id].owner.reset();
    actors_[id].effect=effect;
    if(!ground)actors_[id].projectileAltitude=altitude;
}

void GameWorld::spawnWeaponSmoke(ClassId weapon,ContentPosition muzzle,float shooterAltitude) {
    // 443bd0,44414e..4441d8: one independent muzzle product per emitted
    // Ammo body. This is neither Hit-fx nor the Ammo's CREATE trail.
    if(weapon.value>=scene_->classes.size())return;
    const auto& properties=scene_->classes[weapon.value].properties;
    const TextNode* metadata=nullptr;
    for(auto it=properties.rbegin();it!=properties.rend();++it)
        if(contentNameIs(it->key,"type") && !it->arguments.empty() &&
           contentNameIs(it->arguments.front(),"weapon")){metadata=&*it;break;}
    if(!metadata)return;
    const auto* smoke=findClass(*scene_,propertyText(metadata->children,"Smoke-fx"));
    if(!smoke)return;
    if(propertyBool(smoke->properties,"Uniq-at-xy"))
        for(size_t i=0;i<actors_.size();++i)
            if((actors_[i].alive || actors_[i].reactivateMs>=0) && scene_->objects[i].classId==smoke->id &&
               scene_->objects[i].position.x==muzzle.x &&
               scene_->objects[i].position.y==muzzle.y)return;
    const auto classId=smoke->id;
    const bool effect=attribute(smoke->properties,"fx")!=nullptr;
    const float altitude=std::trunc(shooterAltitude)+
                         std::trunc(propertyNumber(metadata->children,"Smoke-altitude"))+2;
    // Unlike43d350, this producer has no436690 camera gate, position clamp,
    // TYPE-FX restriction or In-ground altitude exception.
    const auto id=spawn(classId,muzzle);
    if(id>=actors_.size())return;
    scene_->objects[id].owner.reset();
    actors_[id].effect=effect;
    actors_[id].projectileAltitude=altitude;
}

bool GameWorld::spawnProjectileHitEffect(const Projectile& shot,bool contact) {
    // 43d350: actor AND terrain contacts supply a nonnull target. Only the
    // fuse/ground terminal path440ebe passes null and selects ground/water FX.
    const auto& source=scene_->classes[shot.classId.value];
    if(!attribute(source.properties,"ammo"))return false;
    const auto subtype=objectSubtype(source.properties,source.sprite);
    if(subtype=="fx" || subtype=="item" || subtype=="lift" || subtype=="gate")return false;
    const float altitude=std::trunc(shot.visual && actors_[*shot.visual].projectileAltitude
        ?*actors_[*shot.visual].projectileAltitude:shot.slopeAltitude);
    std::string effect=shot.hitEffect;
    if(!contact) {
        if(altitude>0)return true;
        bool water=false;
        if(shot.position.x>=0 && shot.position.y>=0 && shot.position.x<level_->mapW*64.f &&
           shot.position.y<level_->mapH*32.f) {
            const auto cell=static_cast<size_t>(shot.position.y/32)*level_->mapW+
                            static_cast<size_t>(shot.position.x/64);
            water=cell<level_->collisionMask.size() && level_->collisionMask[cell]==0x100;
        }
        effect=water?"Water-Splash":"Bullet-Ground-Impact";
    }
    const auto* fx=findClass(*scene_,effect);
    if(!fx)return true;
    if(!attribute(fx->properties,"fx"))return false;
    //43dc40 uses the unclamped point's cell and x87 C3 equality. In the
    //distance-two singular case __ftol's low word is0 and unordered components
    //act as wildcards within that cell; do not normalize the point beforehand.
    if(propertyBool(fx->properties,"Uniq-at-xy")){
        const auto cell=actorCells_.coordinate(nativeImpactCoordinateWord(shot.position.x),
                                               nativeImpactCoordinateWord(shot.position.y));
        if(cell!=NativeActorCells::Missing){
            for(size_t id=actorCells_.heads[cell];id!=NativeActorCells::Missing;id=actorCells_.next[id]){
                if(id>=actors_.size())break;
                if(scene_->objects[id].classId==fx->id&&
                   nativeImpactCoordinateEqual(scene_->objects[id].position.x,shot.position.x)&&
                   nativeImpactCoordinateEqual(scene_->objects[id].position.y,shot.position.y))return false;
            }
        }
    }
    if(level_->mapW<=0 || level_->mapH<=0)return false;
    const float extentX=level_->surfaceWidth>0?level_->surfaceWidth:level_->mapW*64;
    const float extentY=level_->surfaceHeight>0?level_->surfaceHeight:level_->mapH*32;
    const ContentPosition position{nativeImpactFxClamp(shot.position.x,extentX),
                                   nativeImpactFxClamp(shot.position.y,extentY)};
    // 43d350 calls436690 after clamping and truncates only for admission.
    // Construction below keeps the independently clamped float position.
    if(!effectPositionAdmitted(position))return true;
    const auto id=spawn(fx->id,position);
    if(id>=actors_.size())return true;
    //43d59f copies the creator pointer AND captured generation verbatim.
    //Recapturing the current owner's generation would revive a stale link.
    scene_->objects[id].owner=shot.owner<actors_.size()?
        std::optional<ObjectId>{ObjectId{static_cast<std::uint32_t>(shot.owner)}}:std::nullopt;
    actors_[id].nativeCreatorGeneration=shot.nativeOwnerGeneration;
    auto& actor=actors_[id];actor.solid=false;actor.enemy=false;actor.effect=true;
    if(!fx->flags.inGround.value_or(false))actor.projectileAltitude=altitude;
    return true;
}

bool GameWorld::reusableTrail(ClassId id) const {
    const auto& p=scene_->classes[id.value].properties;
    if(attribute(p,"explosion") || attribute(p,"ammo") ||
       attribute(p,"item") || attribute(p,"generator"))return false;
    // Shipped smoke/Pulsar trails have no TYPE FX: they are Fixed, zero-radius
    // objects with an authored lifetime. Reuse only inert CREATE products.
    if(propertyNumber(p,"Bounding-box-size")!=0 || propertyNumber(p,"Speed-max")!=0 ||
       propertyNumber(p,"Reward")!=0 || propertyNumber(p,"Kill-Count")!=0)return false;
    const auto& cls=scene_->classes[id.value];
    if(cls.behaviours.size()!=1 || !contentNameIs(cls.behaviours.front(),"fixed"))return false;
    if(!attribute(p,"fx") && propertyNumber(p,"Life-time",-1)<0)return false;
    if(propertyBool(p,"Saving-zone") || propertyBool(p,"Saving-plot"))return false;
    for(const auto& n:p) {
        if(contentNameIs(n.key,"add") || contentNameIs(n.key,"have") || contentNameIs(n.key,"random") || contentNameIs(n.key,"create"))return false;
        if(contentNameIs(n.key,"type") && !n.arguments.empty() &&
           !contentNameIs(n.arguments[0],"fx") && !contentNameIs(n.arguments[0],"random") && !contentNameIs(n.arguments[0],"guardian"))return false;
    }
    return true;
}
size_t GameWorld::spawnTrail(ClassId cls,ContentPosition position) {
    auto found=freeTrailSlots_.find(cls.value);
    if(found!=freeTrailSlots_.end() && !found->second.empty()) {
        const size_t id=*found->second.begin();found->second.erase(found->second.begin());
        auto& object=scene_->objects[id];object.position=position;object.owner.reset();
        const auto revision=actors_[id].visualRevision+1;
        actors_[id]=makeActor(object);actors_[id].pooledTrail=true;actors_[id].visualRevision=revision;
        initializeNativeIdentity(id);
        updateSpiderNavigation(id,{},false);
        //Reusable products have no CREATE, but still enter the native graph
        //before their final cell link, just like a newly allocated factory body.
        synchronizeActorCell(id);
        audioEvent(AudioEventKind::Spawn,id,scene_->classes[cls.value].name);
        return id;
    }
    const size_t id=spawn(cls,position);
    if(id<actors_.size())actors_[id].pooledTrail=reusableTrail(cls);
    return id;
}
void GameWorld::configureProjectileCreates(Projectile& shot,bool fromConstructor) {
    shot.creates.clear();
    const auto& properties=scene_->classes[shot.classId.value].properties;
    std::vector<bool> copiedConstructor;
    if(fromConstructor && shot.visual)copiedConstructor.resize(actors_[*shot.visual].creates.size());
    for(const auto& node:properties) {
        if(folded(node.key)!="create")continue;
        const auto* target=findClass(*scene_,propertyText(node.children,"Name"));
        if(!target)continue; // The content ledger retains unresolved references.
        // This implementation covers the authored fixed projectile trails.
        // Moving CREATE products need their separate native launch controller.
        const bool fixed=std::any_of(target->behaviours.begin(),target->behaviours.end(),
            [](const auto& b){return folded(b)=="fixed";});
        if(!fixed || propertyBool(node.children,"Random-dir") || propertyBool(node.children,"Set-speed") ||
           propertyNumber(node.children,"Distance")!=0) {
            diagnostics_.push_back("projectile CREATE launch modifiers not implemented: "+target->name);
            continue;
        }
        ProjectileCreate create;create.classId=target->id;
        create.nextMs=std::max(0.f,propertyNumber(node.children,"Delay"));
        create.repeatMs=std::max(0.f,propertyNumber(node.children,"Repeat-timeout"));
        create.currentPosition=propertyBool(node.children,"At-current-position");
        create.trailStep=propertyNumber(node.children,"Trail-step");
        if(const auto* offset=contentProperty(node.children,"Offset")) {
            if(!offset->values.empty())create.offset.x=std::strtof(offset->values[0].c_str(),nullptr);
            if(offset->values.size()>1)create.offset.y=std::strtof(offset->values[1].c_str(),nullptr);
            if(!std::isfinite(create.offset.x)||!std::isfinite(create.offset.y))continue;
        }
        // A new fast shot advances before the first render in our fixed-step
        // loop. Connect its first visual segment to the authored launch point,
        // rather than leaving that entire first displacement invisible. This
        // presentation adapter does not move the projectile or change contacts.
        // Do not fabricate missing history when restoring an older live shot.
        if(create.trailStep>0 && create.currentPosition && shot.ageMs==0)
            create.previousOrigin=ContentPosition{std::trunc(shot.position.x+create.offset.x),
                                                  std::trunc(shot.position.y+create.offset.y)};
        // v1-v5 did not preserve CREATE schedules. Resume old in-flight shots
        // without replaying their entire trail history; v6 retains exact timers.
        if(shot.ageMs>create.nextMs) {
            if(create.repeatMs>0)create.nextMs=shot.ageMs+create.repeatMs;
            else create.done=true;
        }
        if(fromConstructor && shot.visual && *shot.visual<actors_.size()) {
            //413800 has already attempted due constructor CREATE records.
            //Carry their timers forward; keep the accepted launch-point trail
            //history adapter independent of the native constructor's history.
            const auto& initial=actors_[*shot.visual].creates;
            for(size_t n=0;n<initial.size();++n) {
                const auto& c=initial[n];
                if(!copiedConstructor[n] && c.classId==create.classId &&
                   c.offset.x==create.offset.x && c.offset.y==create.offset.y &&
                   c.repeatMs==create.repeatMs && c.currentPosition==create.currentPosition) {
                    create.nextMs=c.nextMs;create.done=c.done;copiedConstructor[n]=true;break;
                }
            }
        }
        shot.creates.push_back(create);
    }
    if(fromConstructor && shot.visual && *shot.visual<actors_.size())actors_[*shot.visual].creates.clear();
}
void GameWorld::updateProjectileCreates(Projectile& shot,ContentPosition previous) {
    // 437b00: one attempt per CREATE per update, no catch-up loop. Default
    // placement uses actor+64/+68 (previous surface position); flag8 selects
    // current surface position. Newly spawned products inherit parent altitude.
    for(auto& create:shot.creates) {
        if(create.done || create.nextMs>shot.ageMs)continue;
        const auto origin=create.currentPosition?shot.position:previous;
        const auto id=spawnTrail(create.classId,{origin.x+create.offset.x,origin.y+create.offset.y});
        if(id>=actors_.size())continue;
        auto& actor=actors_[id];
        actor.enemy=false;actor.solid=false;
        actor.effect=attribute(scene_->objects[id].properties,"fx")!=nullptr;
        actor.projectileAltitude=shot.visual && actors_[*shot.visual].projectileAltitude
            ?*actors_[*shot.visual].projectileAltitude
            :shot.verticalBounce?projectileBounceAltitude(shot.motion.magnitude,shot.ageMs,shot.bouncePeriod)
            :propertyNumber(scene_->classes[shot.classId.value].properties,"Altitude");
        // 437b00 constructs an independent child with no creator. Only the
        // damaging Ammo body and TYPE Explosion transfer retain ancestry.
        scene_->objects[id].owner.reset();
        if(create.trailStep>0) {
            if(create.previousOrigin && actor.effect)actor.trailSegment=FxTrailSegment{*create.previousOrigin,create.trailStep};
            const auto here=scene_->objects[id].position;
            create.previousOrigin=ContentPosition{std::trunc(here.x),std::trunc(here.y)};
        }
        if(create.repeatMs==0)create.done=true;
        else create.nextMs+=create.repeatMs;
    }
}
}
