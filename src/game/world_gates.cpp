#include "game/world.h"
#include <cmath>
namespace vyruz {
void GameWorld::initializeGates() {
    for(size_t i=0;i<actors_.size();++i) {
        auto& actor=actors_[i];actor.gate=makeGateConfig(scene_->objects[i]);
        if(!actor.gate)continue;
        actor.gateState=initializeGate(*actor.gate);actor.gateRevision=1;
        actor.gateAnimationFinished=false;actor.solid=(actor.gateState.flags&GateOpened)==0;
        actor.enemy=false;actor.directional=false;
    }
}
void GameWorld::finishGateAnimation(size_t id,uint64_t revision) {
    if(id<actors_.size()&&(actors_[id].gate||actors_[id].lift)&&actors_[id].gateRevision==revision)
        actors_[id].gateAnimationFinished=true;
}
void GameWorld::updateGates(float milliseconds) {
    for(size_t i=0;i<actors_.size();++i)updateGate(i,milliseconds);
}
void GameWorld::updateGate(size_t i,float milliseconds) {
        auto& gate=actors_[i];if(!gate.alive||!gate.gate)return;
        const auto p=scene_->objects[i].position;
        std::optional<size_t> authorized;bool occupied=false;
        for(size_t j=0;j<actors_.size();++j) {
            const auto& actor=actors_[j];if(j==i||!actor.alive)continue;
            const auto q=scene_->objects[j].position;
            if(actor.solid&&nativeDistanceSquared(p,q)<(gate.radius+actor.radius)*(gate.radius+actor.radius))occupied=true;
            // Supported moving actors correspond to the native non-item,
            // non-decoration eligibility set; sensor is not player-only.
            if(j!=player_&&!actor.enemy)continue;
            if(scene_->objects[j].flags.invisible.value_or(false))continue;
            if(std::abs(static_cast<int>(p.x/64)-static_cast<int>(q.x/64))>2||
               std::abs(static_cast<int>(p.y/32)-static_cast<int>(q.y/32))>2)continue;
            if(gate.gateState.keyId&&!hasKey(actor.heldKeys,gate.gateState.keyId)&&
                !actor.keys.contains(gate.gateState.keyId))continue;
            if(!authorized)authorized=j;
        }
        const auto output=advanceGate(*gate.gate,gate.gateState,
            {milliseconds,gate.gateAnimationFinished,authorized.has_value(),occupied});
        gate.solid=output.blocking;
        if(output.restartAnimation){++gate.gateRevision;gate.gateAnimationFinished=false;}
        if(output.consumeKeyId&&authorized){
            const bool code=(gate.gateState.flags&GateNeedsCode)!=0;
            if(consumeHeldKey(*authorized,output.consumeKeyId) && *authorized==player_)
                notifications_.keyUsed(output.consumeKeyId,code);
        }
}
} // namespace vyruz
