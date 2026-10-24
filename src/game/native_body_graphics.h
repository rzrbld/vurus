#pragma once

#include "render/animation.h"
#include <array>
#include <cmath>
#include <limits>
#include <type_traits>

namespace vyruz {

// Private correctness kernel. Metadata ownership, admission phase, drawing and
// persistence are deliberately outside this value type. No native pointers or
// device voice handles are serialized here.
enum class NativeBodyGraphicsKind : uint32_t { Ammo=0x10, Thing=0x15 };
enum class NativeBodyGraphicsBinding : uint8_t { Unknown, MissingArray, Present };
struct NativeBodyGraphicsSlot {
    uint32_t sequence=0;                 // Native ANM+c; 0 is unbound, not missing array.
    AnimationCursorState animation{};    // Native +20, +0, +14; duration unit is 14ms.
    uint64_t audioIdentity=0;            // Portable opaque key, NOT native ANM+30.
    bool audioRequested=false;          // A Play was requested, not proof of a live mixer voice.
};
struct NativeBodyGraphicsState {
    static constexpr uint32_t SlotCount=24;
    NativeBodyGraphicsBinding binding=NativeBodyGraphicsBinding::Unknown;
    NativeBodyGraphicsKind kind=NativeBodyGraphicsKind::Ammo;
    uint32_t baseSequence=0;
    std::array<NativeBodyGraphicsSlot,SlotCount> slots{};
};
static_assert(std::is_trivially_copyable_v<NativeBodyGraphicsState>);

// Metadata-independent validation for transactional save decoding. Complete
// sequence lengths/flags/durations are additionally validated against the bank.
inline bool validNativeBodyGraphicsShape(const NativeBodyGraphicsState& state) {
    if(state.kind!=NativeBodyGraphicsKind::Ammo && state.kind!=NativeBodyGraphicsKind::Thing)return false;
    if(state.binding!=NativeBodyGraphicsBinding::Unknown && state.binding!=NativeBodyGraphicsBinding::MissingArray && state.binding!=NativeBodyGraphicsBinding::Present)return false;
    if(state.binding!=NativeBodyGraphicsBinding::Present && state.baseSequence)return false;
    for(uint32_t h=0;h<24;++h) {
        const auto& s=state.slots[h];const auto& a=s.animation;
        const uint64_t expected=state.binding==NativeBodyGraphicsBinding::Present && state.baseSequence?
            uint64_t(state.baseSequence)+(state.kind==NativeBodyGraphicsKind::Ammo?h:0):0;
        if(expected>UINT32_MAX || s.sequence!=expected || !std::isfinite(a.remaining))return false;
        if(!s.sequence) {if(a.cursor || a.flags!=8 || a.remaining!=0)return false;}
        else if(!(a.flags&8) && (!a.cursor || a.remaining<=0))return false;
    }
    return true;
}

enum class NativeBodyGraphicsResult : uint8_t {
    Ok, InvalidKind, InvalidSequence, InvalidIdentity, InvalidHeading,
    InvalidElapsed, InvalidCursor, Unknown, MissingArray, Unbound, Stopped
};
enum class NativeBodyGraphicsAudioKind : uint8_t { Entered, Held, Forget };
struct NativeBodyGraphicsAudioEvent {
    NativeBodyGraphicsAudioKind kind;
    uint32_t slot=0,sequence=0,subframe=0;
    uint64_t identity=0;
    uint32_t command=0,sample=0; // Raw authored bits, including global-sample bit.
};
using NativeBodyGraphicsAudioSink=std::function<void(const NativeBodyGraphicsAudioEvent&)>;
using NativeBodyGraphicsIdentities=std::array<uint64_t,NativeBodyGraphicsState::SlotCount>;

inline bool nativeBodyGraphicsIdentities(uint64_t first,NativeBodyGraphicsIdentities& out) {
    if(!first || first>std::numeric_limits<uint64_t>::max()-(out.size()-1))return false;
    for(size_t i=0;i<out.size();++i)out[i]=first+i;
    return true;
}
inline bool nativeBodyGraphicsValidIdentities(const NativeBodyGraphicsIdentities& ids) {
    for(size_t i=0;i<ids.size();++i) {
        if(!ids[i])return false;
        for(size_t j=0;j<i;++j)if(ids[i]==ids[j])return false;
    }
    return true;
}
inline void nativeBodyGraphicsForget(const NativeBodyGraphicsState& state,
                                     const NativeBodyGraphicsAudioSink& sink) {
    if(!sink)return;
    for(uint32_t h=0;h<state.slots.size();++h) {
        const auto& s=state.slots[h];
        if(s.audioIdentity && s.audioRequested)sink({NativeBodyGraphicsAudioKind::Forget,h,s.sequence,
                                s.animation.cursor,s.audioIdentity,0,0});
    }
}
inline void nativeBodyGraphicsReset(NativeBodyGraphicsState& state,
                                    NativeBodyGraphicsBinding binding,
                                    NativeBodyGraphicsKind kind,
                                    const NativeBodyGraphicsAudioSink& sink={}) {
    // Known missing array must come from resolved constructor inputs, never a
    // missing renderer channel. Present with base=0 represents an unbound array.
    nativeBodyGraphicsForget(state,sink);
    state={};state.binding=binding;state.kind=kind;
}
inline void nativeBodyGraphicsEntered(NativeBodyGraphicsSlot& slot,uint32_t heading,
                                      const AnimationCursor& cursor,const GafSubframe& frame,
                                      const NativeBodyGraphicsAudioSink& sink) {
    if(!(frame.command&1) || !frame.sample)return;
    slot.audioRequested=true;
    if(sink)sink({NativeBodyGraphicsAudioKind::Entered,heading,slot.sequence,
                  cursor.capture().cursor,slot.audioIdentity,frame.command,frame.sample});
}
inline void nativeBodyGraphicsHeld(const NativeBodyGraphicsSlot& slot,uint32_t heading,
                                   const NativeBodyGraphicsAudioSink& sink) {
    // The consumer validates actual voice ownership. Inactive headings receive
    // no Held and changing heading never sends Forget to the previous heading.
    if(sink && slot.audioRequested && !(slot.animation.flags&8) && slot.animation.remaining>0)
        sink({NativeBodyGraphicsAudioKind::Held,heading,slot.sequence,
              slot.animation.cursor,slot.audioIdentity,0,0});
}

inline NativeBodyGraphicsResult nativeBodyGraphicsBind(
    NativeBodyGraphicsState& state,NativeBodyGraphicsKind kind,const GafAnimation& bank,
    uint32_t base,const NativeBodyGraphicsIdentities& ids,
    const NativeBodyGraphicsAudioSink& sink={}) {
    if(kind!=NativeBodyGraphicsKind::Ammo && kind!=NativeBodyGraphicsKind::Thing)
        return NativeBodyGraphicsResult::InvalidKind;
    if(!nativeBodyGraphicsValidIdentities(ids))return NativeBodyGraphicsResult::InvalidIdentity;
    // 418d36 increments Ammo sequence; 418e61 only increments Thing record.
    const uint64_t last=uint64_t(base)+(kind==NativeBodyGraphicsKind::Ammo?23:0);
    if(base && last>bank.sequences.size())return NativeBodyGraphicsResult::InvalidSequence;
    // No events and no state change occur on validation failure above. A native
    // invalid GAF-index is fatal, so caller must not turn failure into Missing.
    NativeBodyGraphicsState next;
    next.binding=NativeBodyGraphicsBinding::Present;next.kind=kind;next.baseSequence=base;
    nativeBodyGraphicsForget(state,sink);
    for(uint32_t h=0;h<next.slots.size();++h) {
        auto& slot=next.slots[h];slot.audioIdentity=ids[h];
        slot.sequence=base?base+(kind==NativeBodyGraphicsKind::Ammo?h:0):0;
        if(!slot.sequence)continue;
        AnimationCursor cursor;
        cursor.reset(bank.sequences[slot.sequence-1],[&](const GafSubframe& frame){
            nativeBodyGraphicsEntered(slot,h,cursor,frame,sink);
        });
        slot.animation=cursor.capture();
        nativeBodyGraphicsHeld(slot,h,sink);
    }
    state=next;
    return NativeBodyGraphicsResult::Ok;
}

inline NativeBodyGraphicsResult nativeBodyGraphicsStatus(const NativeBodyGraphicsState& state,
                                                         uint32_t heading) {
    if(state.binding==NativeBodyGraphicsBinding::Unknown)return NativeBodyGraphicsResult::Unknown;
    if(state.binding==NativeBodyGraphicsBinding::MissingArray)return NativeBodyGraphicsResult::MissingArray;
    if(heading>=state.slots.size())return NativeBodyGraphicsResult::InvalidHeading;
    const auto& s=state.slots[heading];
    if(!s.sequence)return NativeBodyGraphicsResult::Unbound;
    return s.animation.flags&8?NativeBodyGraphicsResult::Stopped:NativeBodyGraphicsResult::Ok;
}
inline NativeBodyGraphicsResult nativeBodyGraphicsAdvance(
    NativeBodyGraphicsState& state,const GafAnimation& bank,uint32_t heading,int32_t elapsedMs,
    const NativeBodyGraphicsAudioSink& sink={}) {
    if(elapsedMs<0)return NativeBodyGraphicsResult::InvalidElapsed;
    const auto status=nativeBodyGraphicsStatus(state,heading);
    if(status!=NativeBodyGraphicsResult::Ok)return status;
    auto& slot=state.slots[heading];
    if(slot.sequence>bank.sequences.size())return NativeBodyGraphicsResult::InvalidSequence;
    const auto& sequence=bank.sequences[slot.sequence-1];
    AnimationCursor cursor;
    if(!cursor.restore(sequence,slot.animation))return NativeBodyGraphicsResult::InvalidCursor;
    cursor.advance(sequence,elapsedMs,false,[&](const GafSubframe& frame){
        nativeBodyGraphicsEntered(slot,heading,cursor,frame,sink);
    });
    slot.animation=cursor.capture();
    nativeBodyGraphicsHeld(slot,heading,sink);
    return NativeBodyGraphicsResult::Ok;
}

enum class NativeBodyGraphicsRetirement : uint8_t { Unknown, Keep, Retire, InvalidHeading };
inline NativeBodyGraphicsRetirement nativeBodyGraphicsRetirement(
    const NativeBodyGraphicsState& state,uint32_t finalHeading,bool runtimeHidden) {
    // 44525e..4452a2: hidden bypass precedes pointer/slot inspection. The tail
    // reads FINAL heading, which can differ from the slot advanced before move.
    if(runtimeHidden)return NativeBodyGraphicsRetirement::Keep;
    switch(nativeBodyGraphicsStatus(state,finalHeading)) {
    case NativeBodyGraphicsResult::Unknown:return NativeBodyGraphicsRetirement::Unknown;
    case NativeBodyGraphicsResult::MissingArray:
    case NativeBodyGraphicsResult::Stopped:return NativeBodyGraphicsRetirement::Retire;
    case NativeBodyGraphicsResult::InvalidHeading:return NativeBodyGraphicsRetirement::InvalidHeading;
    default:return NativeBodyGraphicsRetirement::Keep; // Sequence0 explicitly survives.
    }
}
inline const GafSubframe* nativeBodyGraphicsFrame(const NativeBodyGraphicsState& state,
                                                 const GafAnimation& bank,uint32_t heading) {
    if(state.binding!=NativeBodyGraphicsBinding::Present || heading>=state.slots.size())return nullptr;
    const auto& slot=state.slots[heading];
    if(!slot.sequence || slot.sequence>bank.sequences.size())return nullptr;
    AnimationCursor cursor;
    const auto& sequence=bank.sequences[slot.sequence-1];
    return cursor.restore(sequence,slot.animation)?cursor.frame(sequence):nullptr;
}
inline bool nativeBodyGraphicsRestoreAudio(NativeBodyGraphicsState& state,
                                           const NativeBodyGraphicsIdentities& ids) {
    if(!nativeBodyGraphicsValidIdentities(ids))return false;
    // Device handles/requests are transient. Do not replay current subframes or
    // alter ANY animation clock on disk restore. Old playback is caller-owned.
    for(size_t h=0;h<state.slots.size();++h) {
        state.slots[h].audioIdentity=ids[h];state.slots[h].audioRequested=false;
    }
    return true;
}

} // namespace vyruz
