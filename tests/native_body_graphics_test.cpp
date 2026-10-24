#include "game/native_body_graphics.h"
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace vyruz;
namespace {
unsigned checks=0;
void require(bool ok,const char* what) {
    ++checks;if(!ok)throw std::runtime_error(what);
}
bool same(const AnimationCursorState& a,const AnimationCursorState& b) {
    return a.cursor==b.cursor && a.flags==b.flags && a.remaining==b.remaining;
}
bool same(const NativeBodyGraphicsState& a,const NativeBodyGraphicsState& b) {
    if(a.binding!=b.binding || a.kind!=b.kind || a.baseSequence!=b.baseSequence)return false;
    for(size_t h=0;h<a.slots.size();++h) {
        const auto& x=a.slots[h];const auto& y=b.slots[h];
        if(x.sequence!=y.sequence || !same(x.animation,y.animation) ||
           x.audioIdentity!=y.audioIdentity || x.audioRequested!=y.audioRequested)return false;
    }
    return true;
}
GafAnimation bank(size_t count,uint32_t frames=3,uint32_t duration=1) {
    GafAnimation out;out.sequences.resize(count);
    for(auto& sequence:out.sequences)for(uint32_t f=0;f<frames;++f) {
        GafSubframe frame;frame.frameIndex=f;frame.duration=duration;
        sequence.subframes.push_back(frame);
    }
    return out;
}
NativeBodyGraphicsIdentities identities(uint64_t first=100) {
    NativeBodyGraphicsIdentities ids;
    require(nativeBodyGraphicsIdentities(first,ids),"identity allocation");return ids;
}
void slotsAndSelection() {
    auto metadata=bank(48);
    NativeBodyGraphicsState state;
    require(nativeBodyGraphicsBind(state,NativeBodyGraphicsKind::Ammo,metadata,25,identities())==
            NativeBodyGraphicsResult::Ok,"Grenade authored base25 binds");
    for(uint32_t h=0;h<24;++h) {
        require(state.slots[h].sequence==25+h,"Ammo native base+h");
        require(state.slots[h].animation.cursor==1 && state.slots[h].animation.remaining==14,
                "45e770 reset enters first frame at zero elapsed");
    }
    // Instruction-derived slot policy: 431ec2 selects heading*72, does not rebind.
    const auto initial=state;
    require(nativeBodyGraphicsAdvance(state,metadata,0,14)==NativeBodyGraphicsResult::Ok,"advance h0");
    require(nativeBodyGraphicsAdvance(state,metadata,0,7)==NativeBodyGraphicsResult::Ok,"advance h0 again");
    require(nativeBodyGraphicsAdvance(state,metadata,1,5)==NativeBodyGraphicsResult::Ok,"switch h1");
    require(nativeBodyGraphicsAdvance(state,metadata,0,0)==NativeBodyGraphicsResult::Ok,"return h0");
    require(state.slots[0].animation.cursor==2 && state.slots[0].animation.remaining==7,"h0 resumes retained clock");
    require(state.slots[1].animation.cursor==1 && state.slots[1].animation.remaining==9,"h1 independent clock");
    for(uint32_t h=2;h<24;++h)require(same(state.slots[h].animation,initial.slots[h].animation),"inactive slots paused");
    auto copied=state;
    nativeBodyGraphicsAdvance(copied,metadata,0,7);
    require(state.slots[0].animation.cursor==2 && copied.slots[0].animation.cursor==3,"value copy independent");
    auto thingBank=bank(1);
    require(nativeBodyGraphicsBind(state,NativeBodyGraphicsKind::Thing,thingBank,1,identities())==
            NativeBodyGraphicsResult::Ok,"Thing only needs one sequence");
    for(const auto& slot:state.slots)require(slot.sequence==1,"Thing SAME authored sequence for all headings");
    nativeBodyGraphicsAdvance(state,thingBank,3,14);
    require(state.slots[3].animation.cursor==2 && state.slots[4].animation.cursor==1,"Thing states remain independent");
}
void transactionsAndBoundaries() {
    auto metadata=bank(24);NativeBodyGraphicsState state;
    std::vector<NativeBodyGraphicsAudioEvent> events;
    const NativeBodyGraphicsAudioSink sink=[&](const auto& e){events.push_back(e);};
    auto ids=identities();
    require(nativeBodyGraphicsBind(state,NativeBodyGraphicsKind::Ammo,metadata,1,ids,sink)==NativeBodyGraphicsResult::Ok,"initial bind");
    events.clear();const auto before=state;
    require(nativeBodyGraphicsBind(state,NativeBodyGraphicsKind::Ammo,metadata,2,ids,sink)==NativeBodyGraphicsResult::InvalidSequence,"range failure");
    require(same(before,state) && events.empty(),"range failure is transactional");
    require(nativeBodyGraphicsBind(state,NativeBodyGraphicsKind::Ammo,metadata,UINT32_MAX,ids,sink)==NativeBodyGraphicsResult::InvalidSequence,"range overflow prevented");
    ids[5]=ids[0];
    require(nativeBodyGraphicsBind(state,NativeBodyGraphicsKind::Ammo,metadata,1,ids,sink)==NativeBodyGraphicsResult::InvalidIdentity,"identity alias rejected");
    require(same(before,state) && events.empty(),"identity failure is transactional");
    auto allocated=identities();const auto oldIds=allocated;
    require(!nativeBodyGraphicsIdentities(UINT64_MAX-22,allocated) && allocated==oldIds,"ID overflow transactional");
    require(!nativeBodyGraphicsIdentities(0,allocated),"zero identity forbidden");
    require(nativeBodyGraphicsAdvance(state,metadata,24,14,sink)==NativeBodyGraphicsResult::InvalidHeading,"no invented modulo");
    require(nativeBodyGraphicsAdvance(state,metadata,0,-1,sink)==NativeBodyGraphicsResult::InvalidElapsed,"negative elapsed rejected");
    require(same(before,state) && events.empty(),"invalid step has no side effects");
    state.slots[0].animation.remaining=1000;
    const auto corrupt=state;
    require(nativeBodyGraphicsAdvance(state,metadata,0,14,sink)==NativeBodyGraphicsResult::InvalidCursor,"invalid active clock rejected");
    require(same(corrupt,state) && events.empty(),"invalid cursor not reset silently");
}
void retirement() {
    NativeBodyGraphicsState state;
    require(nativeBodyGraphicsRetirement(state,0,false)==NativeBodyGraphicsRetirement::Unknown,"unknown not missing");
    nativeBodyGraphicsReset(state,NativeBodyGraphicsBinding::MissingArray,NativeBodyGraphicsKind::Ammo);
    require(nativeBodyGraphicsRetirement(state,UINT32_MAX,false)==NativeBodyGraphicsRetirement::Retire,"missing array retires before heading load");
    require(nativeBodyGraphicsRetirement(state,UINT32_MAX,true)==NativeBodyGraphicsRetirement::Keep,"hidden bypass before all inspection");
    auto metadata=bank(24,1);
    nativeBodyGraphicsBind(state,NativeBodyGraphicsKind::Ammo,metadata,0,identities());
    require(nativeBodyGraphicsStatus(state,0)==NativeBodyGraphicsResult::Unbound,"present seq0 explicit");
    require(nativeBodyGraphicsRetirement(state,0,false)==NativeBodyGraphicsRetirement::Keep,"present seq0 survives");
    require(nativeBodyGraphicsAdvance(state,metadata,0,14)==NativeBodyGraphicsResult::Unbound,"unbound does not advance");
    nativeBodyGraphicsBind(state,NativeBodyGraphicsKind::Ammo,metadata,1,identities());
    nativeBodyGraphicsAdvance(state,metadata,0,14);
    require(nativeBodyGraphicsRetirement(state,0,false)==NativeBodyGraphicsRetirement::Retire,"final heading0 stopped");
    require(nativeBodyGraphicsRetirement(state,1,false)==NativeBodyGraphicsRetirement::Keep,"post-move final heading1 still active");
    require(nativeBodyGraphicsAdvance(state,metadata,0,14)==NativeBodyGraphicsResult::Stopped,"stopped no restart");
    AnimationCursor held;held.holdLast(metadata.sequences[1]);
    state.slots[1].animation=held.capture();
    require(nativeBodyGraphicsFrame(state,metadata,1)!=nullptr,"stopped hold-last still has frame");
    require(nativeBodyGraphicsRetirement(state,1,false)==NativeBodyGraphicsRetirement::Retire,"retirement reads flags8 not frame null");
    metadata.sequences[2].subframes[0].frameIndex=GafSubframe::Blank;
    require(nativeBodyGraphicsFrame(state,metadata,2)->frameIndex==GafSubframe::Blank,"blank frame remains bound");
    require(nativeBodyGraphicsRetirement(state,2,false)==NativeBodyGraphicsRetirement::Keep,"blank active is not complete");
    metadata.sequences[3].subframes.clear();
    nativeBodyGraphicsBind(state,NativeBodyGraphicsKind::Ammo,metadata,1,identities());
    require(nativeBodyGraphicsStatus(state,3)==NativeBodyGraphicsResult::Stopped,"empty bound sequence stops on reset");
}
void audio() {
    auto metadata=bank(1,2);
    // Bitmap blank does not suppress ANM sound; preserve raw global sample bit.
    auto& first=metadata.sequences[0].subframes[0];
    first.frameIndex=GafSubframe::Blank;first.command=0x10001;first.sample=7;
    NativeBodyGraphicsState state;std::vector<NativeBodyGraphicsAudioEvent> events;
    const NativeBodyGraphicsAudioSink sink=[&](const auto& e){events.push_back(e);};
    nativeBodyGraphicsBind(state,NativeBodyGraphicsKind::Thing,metadata,1,identities(),sink);
    require(events.size()==48,"24 first-subframe entries and same-call Held requests");
    for(uint32_t h=0;h<24;++h) {
        const auto& entered=events[2*h];const auto& held=events[2*h+1];
        require(entered.kind==NativeBodyGraphicsAudioKind::Entered && entered.slot==h &&
                entered.identity==100+h && entered.sequence==1 && entered.subframe==1 &&
                entered.command==0x10001 && entered.sample==7,"constructor per-slot audio order/identity");
        require(held.kind==NativeBodyGraphicsAudioKind::Held && held.identity==entered.identity,"same-call held ownership request");
    }
    events.clear();
    nativeBodyGraphicsAdvance(state,metadata,0,0,sink);
    require(events.size()==1 && events[0].kind==NativeBodyGraphicsAudioKind::Held,"zero step no duplicate sample");
    events.clear();nativeBodyGraphicsAdvance(state,metadata,1,5,sink);
    require(events.size()==1 && events[0].identity==101 && events[0].kind==NativeBodyGraphicsAudioKind::Held,"heading switch only holds selected voice");
    events.clear();nativeBodyGraphicsAdvance(state,metadata,0,14,sink);
    require(events.size()==1 && events[0].identity==100 && events[0].kind==NativeBodyGraphicsAudioKind::Held,"return retains identity without Forget/replay");
    auto saved=state;
    require(nativeBodyGraphicsRestoreAudio(state,identities(1000)),"restore new transient identities");
    for(uint32_t h=0;h<24;++h)require(same(state.slots[h].animation,saved.slots[h].animation) &&
         !state.slots[h].audioRequested && state.slots[h].audioIdentity==1000+h,"restore preserves clocks, forgets request state");
    events.clear();nativeBodyGraphicsAdvance(state,metadata,0,0,sink);
    require(events.empty(),"restore does not replay/hold old sample");
    nativeBodyGraphicsReset(state,NativeBodyGraphicsBinding::Unknown,NativeBodyGraphicsKind::Ammo,sink);
    require(events.empty(),"restored unused identities have no voice mapping to Forget");
    nativeBodyGraphicsBind(state,NativeBodyGraphicsKind::Thing,metadata,1,identities(),sink);
    events.clear();nativeBodyGraphicsReset(state,NativeBodyGraphicsBinding::Unknown,NativeBodyGraphicsKind::Ammo,sink);
    require(events.size()==24,"true reset forgets requested voices only");
    for(uint32_t h=0;h<24;++h)require(events[h].kind==NativeBodyGraphicsAudioKind::Forget &&
        events[h].identity==100+h,"Forget is identity release, not Stop");
}
}
int main() {
    try { slotsAndSelection();transactionsAndBoundaries();retirement();audio(); }
    catch(const std::exception& e) { std::cerr<<"FAIL: "<<e.what()<<'\n';return 1; }
    std::cout<<checks<<" private body graphics checks passed\n";
}
