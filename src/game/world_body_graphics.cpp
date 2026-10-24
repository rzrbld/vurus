#include "game/world.h"
#include "game/properties.h"
#include "render/native_render_cells.h"
#include <cmath>
#include <algorithm>
#include <stdexcept>

namespace vyruz {
const GafAnimation* GameWorld::bodyGraphicsBank(const std::string& path) {
    auto key=folded(path);std::replace(key.begin(),key.end(),char(92),char(47));
    auto at=bodyGraphicsBanks_.find(key);
    if(at==bodyGraphicsBanks_.end()) {
        std::vector<uint8_t> bytes;GafAnimation bank;std::optional<GafAnimation> decoded;
        if(effectReader_ && effectReader_(path,bytes)) {
            if(!decodeGafAnimation(bytes,bank,GafDecodeMode::Metadata))throw std::runtime_error("Invalid body graphics bank: "+path);
            decoded=std::move(bank);
        }
        at=bodyGraphicsBanks_.emplace(key,std::move(decoded)).first;
        if(!at->second)diagnostics_.push_back("Body graphics unavailable: "+path);
    }
    return at->second?&*at->second:nullptr;
}
NativeBodyGraphicsAudioSink GameWorld::bodyGraphicsSink(size_t id) {
    return [this,id](const NativeBodyGraphicsAudioEvent& event) {
        bodyGraphicsAudio_.push_back({id,scene_->objects[id].sprite.path,event});
    };
}
void GameWorld::forgetBodyGraphics(size_t id) {
    if(id>=actors_.size() || !actors_[id].bodyGraphics)return;
    nativeBodyGraphicsForget(*actors_[id].bodyGraphics,bodyGraphicsSink(id));
}
void GameWorld::initializeBodyGraphics(size_t id) {
    auto& a=actors_[id];const auto& o=scene_->objects[id];
    if(a.nativeSubtype!=0x10 && a.nativeSubtype!=0x15)return;
    a.bodyGraphicsGraphicUpdate=propertyBool(scene_->classes[o.classId.value].properties,"Graphic-update-needed");
    if(a.bodyGraphicsLegacy)return;
    a.bodyGraphicsHidden=o.flags.invisible.value_or(false);
    a.bodyGraphics.emplace();
    const auto kind=static_cast<NativeBodyGraphicsKind>(a.nativeSubtype);
    a.bodyGraphics->kind=kind;
    // A graphics-disabled World has no initialized native graphics pipeline.
    // Keep its state Unknown even when a geometry-only fixture omits sprites.
    // Configured game loads still distinguish proven missing arrays below.
    if(!effectReader_)return;
    //417b30's two fences are independent: either bank pointer and either
    //index suffice for allocation, even when neither matching pair is bound.
    const bool anyBank=!o.sprite.path.empty() || !o.shadow.path.empty();
    const bool anyIndex=o.sprite.sequence.value || o.shadow.sequence.value;
    if(!anyBank || !anyIndex) {
        nativeBodyGraphicsReset(*a.bodyGraphics,NativeBodyGraphicsBinding::MissingArray,kind);return;
    }
    const auto* bank=o.sprite.path.empty()?nullptr:bodyGraphicsBank(o.sprite.path);
    const auto* shadow=o.shadow.path.empty()?nullptr:bodyGraphicsBank(o.shadow.path);
    if((!o.sprite.path.empty() && !bank) || (!o.shadow.path.empty() && !shadow))return;
    if(!bank || !o.sprite.sequence.value) {
        nativeBodyGraphicsReset(*a.bodyGraphics,NativeBodyGraphicsBinding::Present,kind);return;
    }
    NativeBodyGraphicsIdentities ids;
    if(!nativeBodyGraphicsIdentities(nextBodyGraphicsIdentity_,ids))throw std::runtime_error("Body graphics audio identity exhausted");
    nextBodyGraphicsIdentity_+=ids.size();
    const auto result=nativeBodyGraphicsBind(*a.bodyGraphics,kind,*bank,o.sprite.sequence.value,ids,bodyGraphicsSink(id));
    if(result!=NativeBodyGraphicsResult::Ok)throw std::runtime_error("Invalid body graphics binding: "+o.sprite.path);
}
void GameWorld::advanceBodyGraphics(size_t id,int32_t milliseconds) {
    if(id>=actors_.size() || !actors_[id].alive || actors_[id].bodyGraphicsLegacy)return;
    if(!actors_[id].bodyGraphics)initializeBodyGraphics(id);
    auto& a=actors_[id];if(!a.bodyGraphics || a.bodyGraphics->binding!=NativeBodyGraphicsBinding::Present || !a.bodyGraphics->baseSequence)return;
    const auto* bank=bodyGraphicsBank(scene_->objects[id].sprite.path);
    if(bank)nativeBodyGraphicsAdvance(*a.bodyGraphics,*bank,a.direction,milliseconds,bodyGraphicsSink(id));
}
void GameWorld::advanceBodyGraphicsAwake(size_t id,int32_t milliseconds) {
    if(id<actors_.size() && actors_[id].bodyGraphicsGraphicUpdate)advanceBodyGraphics(id,milliseconds);
}
bool GameWorld::retireBodyGraphics(size_t id) {
    if(id>=actors_.size())return false;
    const auto& a=actors_[id];if(!a.alive || a.bodyGraphicsLegacy || !a.bodyGraphics || (a.nativeSubtype!=0x10 && a.nativeSubtype!=0x15))return false;
    const bool hidden=a.bodyGraphicsHidden;
    if(nativeBodyGraphicsRetirement(*a.bodyGraphics,a.direction,hidden)!=NativeBodyGraphicsRetirement::Retire)return false;
    forgetBodyGraphics(id);
    destroy(id,std::nullopt); // Full native removal/CREATE/held/manager path.
    return !actors_[id].alive;
}
void GameWorld::advanceBodyGraphicsPhase(BodyGraphicsView view,BodyGraphicsPhase phase) {
    if(!scene_ || !level_ || view.suspended)return;
    const auto window=nativeRenderWindow(view.translationX,view.translationY,view.width,view.height,level_->mapW,level_->mapH);
    for(bool ground:{true,false})visitNativeRenderCells(actorCells_,window,[&](size_t id) {
        if(id>=actors_.size())return;
        const auto& a=actors_[id];const auto& o=scene_->objects[id];
        if(!a.alive || (a.nativeSubtype!=0x10 && a.nativeSubtype!=0x15) ||
           o.flags.inGround.value_or(false)!=ground)return;
        if(phase==BodyGraphicsPhase::VisibleUpdate && view.playerOnly && id!=player_)return;
        // Mode0 skips timers for Graphic-needed; mode1 here skips the same set
        // through433b30's skipGraphic argument. Hidden/detail filtering is later.
        if(a.bodyGraphicsGraphicUpdate)return;
        advanceBodyGraphics(id,bodyGraphicsStepMs_);
    });
    // Retirement is NOT done here: native common World tail observes it only
    // on the next admitted visit, using final heading after that visit's motion.
}
bool GameWorld::validateBodyGraphics(const ActorState& a,const ObjectPlacement& o,std::string& error) {
    if(!a.bodyGraphics) {
        if(!a.bodyGraphicsLegacy && (a.nativeSubtype==0x10 || a.nativeSubtype==0x15)) {error="Missing body graphics history";return false;}
        return true;
    }
    const auto& state=*a.bodyGraphics;
    if(!validNativeBodyGraphicsShape(state)) {error="Invalid body graphics state shape";return false;}
    if(a.bodyGraphicsLegacy || (a.nativeSubtype!=0x10 && a.nativeSubtype!=0x15) ||
       state.kind!=static_cast<NativeBodyGraphicsKind>(a.nativeSubtype)) {error="Invalid body graphics owner";return false;}
    if(state.binding!=NativeBodyGraphicsBinding::Present) {
        if(state.binding!=NativeBodyGraphicsBinding::Unknown && state.binding!=NativeBodyGraphicsBinding::MissingArray) {error="Invalid body graphics binding enum";return false;}
        if(state.baseSequence) {error="Unbound body graphics has base sequence";return false;}
        for(const auto& slot:state.slots)if(slot.sequence || slot.animation.cursor || slot.animation.flags!=8 || slot.animation.remaining!=0) {error="Invalid unbound body graphics clock";return false;}
        return true;
    }
    if(state.baseSequence!=(o.sprite.path.empty()?0:o.sprite.sequence.value)) {error="Body graphic resource revision mismatch";return false;}
    const auto* bank=o.sprite.path.empty()?nullptr:bodyGraphicsBank(o.sprite.path);
    for(uint32_t h=0;h<24;++h) {
        const auto& slot=state.slots[h];
        if(slot.sequence!=(state.baseSequence?state.baseSequence+(state.kind==NativeBodyGraphicsKind::Ammo?h:0):0)) {error="Invalid directional sequence";return false;}
        if(!slot.sequence)continue;
        AnimationCursor cursor;
        if(!bank || slot.sequence>bank->sequences.size() || !cursor.restore(bank->sequences[slot.sequence-1],slot.animation)) {error="Invalid body graphics clock";return false;}
    }
    return true;
}
} // namespace vyruz
