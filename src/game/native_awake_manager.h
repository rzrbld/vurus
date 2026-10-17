#pragma once
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <optional>
#include <vector>

namespace vyruz {
// Original world+2010 vector: append enrollment, sparse physical release and
//forward hole filling during4406e0. This is separate from native spatial cells.
struct NativeAwakeManager {
    using Identity=std::uint64_t;
    static constexpr Identity Missing=UINT64_MAX;
    std::vector<Identity> entries;
    std::size_t append(Identity id) {
        entries.push_back(id);return entries.size()-1; //405760/419ca0.
    }
    void erase(std::size_t index) {
        if(index<entries.size())entries[index]=Missing; //41a430, no compaction here.
    }
    // Caller visits the returned body before requesting another one. It may
    //append births or clear entries: the next test uses the live vector size.
    template<class Valid,class Moved>
    std::optional<Identity> next(std::size_t& cursor,Valid valid,Moved moved) {
        while(cursor<entries.size()) {
            const auto index=cursor++;
            if(entries[index]==Missing) {
                auto next=index+1;
                //440766 skips a lone trailing hole without trimming it.
                if(next>=entries.size())continue;
                while(next<entries.size() && entries[next]==Missing)++next;
                if(next>=entries.size()) {
                    entries.resize(index);return {};
                }
                entries[index]=entries[next];entries[next]=Missing;
                moved(entries[index],index);
            }
            if(!valid(entries[index])){entries[index]=Missing;continue;}
            return entries[index];
        }
        return {};
    }
};
struct NativeAwakeRestDecision { bool rested=false,sleep=false; };
inline NativeAwakeRestDecision nativeAwakeRestDecision(float displacementX,float displacementZ,
    std::uint32_t kind,std::uint32_t subtype,bool classPresent,std::uint32_t classFlags1c,
    std::uint32_t actorFlags,std::uint32_t bodyPose,std::uint32_t desiredBodyPose) {
    //44169a..441750 tests x87 equality only, so unordered is accepted here.
    const auto equalZero=[](float v){return v==0 || std::isnan(v);};
    const auto subtypeBit=std::uint32_t(1)<<((subtype-0x10)&31);
    const bool rested=equalZero(displacementX)&&equalZero(displacementZ)&&
        ((subtypeBit&0x1e)!=0 || bodyPose==desiredBodyPose);
    return {rested,rested&&kind==3&&classPresent&&!(classFlags1c&0x10)&&(actorFlags&0x8000000)};
}
inline bool nativeAwakeEnrollmentAllowed(bool awake,std::size_t index) {
    //419caf/419cb9: direct duplicate index0 is accepted; callers generally
    //test the awake flag first. Do not silently replace with set membership.
    return !awake || index==0;
}
} //namespace vyruz
