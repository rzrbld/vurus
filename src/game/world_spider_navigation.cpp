#include "game/world.h"
#include "game/properties.h"
#include "game/spider_contact_native.h"
#include "game/native_body_shape.h"
#include "game/native_contact_graph.h"
#include "game/native_navigation_filter.h"
#include <algorithm>
#include <limits>
namespace vyruz {
namespace {
constexpr uint64_t DecorBit=uint64_t(1)<<63;
constexpr uint64_t ProjectileBit=uint64_t(1)<<62;
constexpr uint64_t DisabledProjectileBit=uint64_t(1)<<61;
constexpr size_t Missing=std::numeric_limits<size_t>::max();
bool navigationSubtypeIs(const ObjectPlacement& o,std::string_view wanted){
    if(const auto* type=contentProperty(o.properties,"Sub-type");type&&!type->values.empty()&&!type->values.front().empty())
        return contentNameEqual(type->values.front(),wanted);
    const std::string_view path=o.sprite.path;
    const auto end=path.find_last_of("/\\");if(end==std::string_view::npos||!end)return false;
    const auto start=path.find_last_of("/\\",end-1);
    const auto begin=start==std::string_view::npos?0:start+1;
    return contentNameEqual(path.substr(begin,end-begin),wanted);
}
bool actorHasNavigationShape(const ActorState& a,uint32_t subtype){
    //41a5d0: FX uses shape1, while small Ammo/Thing uses shape2
    // (a point BBOX, still graphobject type2 rather than a routepoint).
    return a.radius>0 && nativeBodyContactShape(subtype,a.radius)!=1;
}
bool actorNavigationEnabled(const ActorState& a,const ObjectPlacement& o,uint32_t subtype){
    if(!a.alive||!actorHasNavigationShape(a,subtype))return false;
    if((o.flags.invisible.value_or(false)||a.ammoHidden)&&
       !propertyBool(o.properties,"Keep-bb-when-invisible"))return false;
    if((subtype==0x13||subtype==0x14||subtype==0x17)&&
       (a.gate||a.lift||!a.generators.empty())&&!a.solid)return false;
    return true;
}
}
NativeNavObstacle GameWorld::actorNavigationBounds(size_t id,uint32_t subtype){
    const auto& a=actors_[id];const auto& o=scene_->objects[id];
    //41a5d0/41b4b0 use the same x87 projection for every native BBOX.
    //A different rounded center can invent an axis crossing at zero motion.
    const auto* history=a.nativeMotionHistory();
    if(!history && actorNavigationProjections_.size()<=id)
        actorNavigationProjections_.resize(actors_.size());
    const auto p=history?Displacement{history->center.x,history->center.y}:
        actorNavigationProjections_[id].resolve(o.position.x,o.position.y,spiderSurfaceToNative);
    const auto shape=nativeBodyContactShape(subtype,a.radius);
    const float r=shape==2?0.f:shape==4?a.radius*.7071067690849304f:a.radius;
    return {p.x-r,p.y-r,p.x+r,p.y+r};
}
void GameWorld::initializeSpiderNavigation(bool projectileNeeded){
    spiderNavigation_.reset();spiderNavigationActors_.clear();actorNavigationProjections_.clear();
    if(!projectileNeeded && !std::any_of(actors_.begin(),actors_.end(),[](const auto& a){return a.contactConfig.nativeEnemy;}))return;
    spiderNavigation_.emplace();spiderNavigationActors_.assign(actors_.size(),Missing);
    struct Entry{int x,y;bool decor;size_t id;};std::vector<Entry> entries;
    for(size_t i=0;i<level_->nativeNavigationDecor.size();++i){const auto& d=level_->nativeNavigationDecor[i];
        if(!d.disabled)entries.push_back({d.cellX,d.cellY,true,i});}
    for(size_t i=0;i<actors_.size();++i)if(actors_[i].alive&&actorHasNavigationShape(actors_[i],nativeDamageSubtype(i))&&
        (actorNavigationEnabled(actors_[i],scene_->objects[i],nativeDamageSubtype(i))||nativeDamageSubtype(i)==0x13||nativeDamageSubtype(i)==0x14)){
        const auto p=scene_->objects[i].position;entries.push_back({int(p.x)/64,int(p.y)/32,false,i});}
    std::stable_sort(entries.begin(),entries.end(),[](const Entry& a,const Entry& b){
        if(a.y!=b.y)return a.y>b.y;
        if(a.x!=b.x)return a.x>b.x;
        if(a.decor!=b.decor)return a.decor;
        return a.decor?a.id<b.id:a.id>b.id;
    });
    for(const auto& e:entries)if(e.decor){const auto& d=level_->nativeNavigationDecor[e.id];
        insertNativeNavigationObject(*spiderNavigation_,DecorBit|e.id,{d.minX,d.minY,d.maxX,d.maxY});
    }else spiderNavigationActors_[e.id]=insertNativeNavigationObject(*spiderNavigation_,e.id,actorNavigationBounds(e.id,nativeDamageSubtype(e.id)));
    // A producer rebuild must include already-existing bare Ammo immediately;
    //waiting for its next projectile visit loses actor->Ammo candidates.
    // Initialization owns this publication; lazy AI/guidance callers need no second pass.
    for(auto& shot:projectiles_){shot.spiderGraphNode=Missing;updateSpiderProjectileNavigation(shot);}
}
void GameWorld::restoreSpiderNavigationIndex(){
    actorNavigationProjections_.clear();
    spiderNavigationActors_.assign(actors_.size(),Missing);
    if(!spiderNavigation_)return;
    spiderNavigation_->xEdges.invalidateInsertionHint();spiderNavigation_->yEdges.invalidateInsertionHint();
    for(size_t n=0;n<spiderNavigation_->objects.size();++n){const auto& o=spiderNavigation_->objects[n];
        if(o.alive&&!o.point&&!(o.reference&(DecorBit|ProjectileBit))&&o.reference<actors_.size())spiderNavigationActors_[o.reference]=n;}
}
void GameWorld::updateSpiderNavigation(size_t id,std::optional<size_t> closestNonContact,bool synchronizeCell){
    if(synchronizeCell)synchronizeActorCell(id);
    if(!spiderNavigation_||id>=actors_.size())return;
    if(spiderNavigationActors_.size()<=id)spiderNavigationActors_.resize(id+1,Missing);
    auto& index=spiderNavigationActors_[id];const auto& actor=actors_[id];
    // Dead/zero-size actors cannot contribute a graph shape. Reject them
    // before resolving subtype properties: expired effects retain their slots,
    // so doing that work on every bounds refresh scales with all past effects.
    if(!actor.alive || actor.radius<=0){
        if(index!=Missing){removeNativeNavigationObject(*spiderNavigation_,index);index=Missing;}return;
    }
    const auto subtype=nativeDamageSubtype(id);
    if(!actorHasNavigationShape(actor,subtype)){
        if(index!=Missing){removeNativeNavigationObject(*spiderNavigation_,index);index=Missing;}return;
    }
    const auto bounds=actorNavigationBounds(id,subtype);
    if(index==Missing){if(actorNavigationEnabled(actor,scene_->objects[id],subtype))index=insertNativeNavigationObject(*spiderNavigation_,id,bounds);}
    else updateNativeContactGraphObject(*spiderNavigation_,index,bounds,closestNonContact);
}
void GameWorld::updateSpiderProjectileNavigation(Projectile& shot){
    if(!spiderNavigation_)return;
    auto& node=shot.spiderGraphNode;
    if(!shot.alive||shot.visual||shot.radius<=0){
        if(node!=Missing&&node<spiderNavigation_->objects.size())removeNativeNavigationObject(*spiderNavigation_,node);
        node=Missing;return;
    }
    auto p=surfaceToNative(shot.position.x,shot.position.y);
    if(shot.nativeBareBody)p={shot.nativeBareBody->motion.center.x,shot.nativeBareBody->motion.center.y};
    const float r=shot.radius<2?0:shot.radius;
    NativeNavObstacle bounds{p.x-r,p.y-r,p.x+r,p.y+r};
    const auto& cls=scene_->classes[shot.classId.value];
    const bool keep=propertyBool(cls.properties,"Keep-bb-when-invisible");
    const bool disabled=!ammoContactEnabled(shot)||
        ((shot.phaseHidden||cls.flags.invisible.value_or(false))&&!keep);
    if(node==Missing)node=insertNativeNavigationObject(*spiderNavigation_,ProjectileBit,bounds);
    else updateNativeNavigationObject(*spiderNavigation_,node,bounds);
    spiderNavigation_->objects[node].reference=ProjectileBit|
        (shot.nativeBodyIdentity?shot.nativeBodyIdentity:uint64_t(node))|(disabled?DisabledProjectileBit:0);
}
std::optional<NativeNavObstacle> GameWorld::spiderNavigationObstacle(size_t actor,std::optional<size_t> target,
        NativeNavPoint from,NativeNavPoint to,NativeNavPoint extent,bool overlap,size_t projectileSelf) const {
    (void)projectileSelf; //All animated Ammo is excluded by the native query.
    if(!spiderNavigation_)return {};
    return queryNativeNavigation(*spiderNavigation_,from,to,extent,overlap,[&](uint64_t ref){
        if(ref&DecorBit){const auto n=ref&~DecorBit;return n<level_->nativeNavigationDecor.size()&&!level_->nativeNavigationDecor[n].water;}
        if(ref&ProjectileBit)return nativeNavigationAnimatedActorAdmitted(3,0x10);
        // 42c5a0/42e400 disable self and the current route target only.
        // The player remains an obstacle for targetless Patrol navigation.
        if(ref>=actors_.size()||ref==actor||(target&&ref==*target))return false;
        const auto& object=scene_->objects[ref];
        if(!nativeNavigationAnimatedActorAdmitted(3,nativeDamageSubtype(ref)))return false;
        if(!actorNavigationEnabled(actors_[ref],object,nativeDamageSubtype(ref)))return false;
        // 40eea0 marks Only-for-player Item attributes graphflag2. This is
        // separate from physical solidity and includes the invisible save area.
        if(navigationSubtypeIs(object,"item"))if(const auto* item=attribute(object.properties,"item"))
            if(propertyBool(item->children,"Only-for-player"))return false;
        return true;
    });
}
std::optional<NativeNavObstacle> GameWorld::projectileNavigationObstacle(const Projectile& shot,std::optional<size_t> target,
        NativeNavPoint from,NativeNavPoint to,NativeNavPoint extent,bool overlap) const {
    // 42c5a0/42d520 disable self and target only. Shooter is not query self.
    return spiderNavigationObstacle(shot.visual.value_or(Missing),target,from,to,extent,overlap,
        shot.nativeBodyIdentity?shot.nativeBodyIdentity:shot.spiderGraphNode);
}
}
