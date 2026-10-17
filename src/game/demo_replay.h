#pragma once
#include <bit>
#include <cmath>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace vyruz {
// Native 4292b0: LE record count followed by 20-byte records. Coordinates are
// surface coordinates, not native isometric axes (438800).
struct DemoRecord {
    uint32_t milliseconds=0, flags=0;
    float x=0, y=0;
    uint32_t direction=0;
};
inline bool decodeDemoReplay(std::span<const uint8_t> bytes,
                             std::vector<DemoRecord>& out,std::string& error) {
    const auto word=[&](size_t p) {
        return uint32_t(bytes[p]) | uint32_t(bytes[p+1])<<8 |
               uint32_t(bytes[p+2])<<16 | uint32_t(bytes[p+3])<<24;
    };
    if(bytes.size()<4) { error="Truncated demo header";return false; }
    const auto count=word(0);
    if(!count || count!=(bytes.size()-4)/20 || (bytes.size()-4)%20) {
        error="Invalid demo record count";return false;
    }
    std::vector<DemoRecord> records;records.reserve(count);
    for(size_t i=0;i<count;++i) {
        const size_t p=4+i*20;
        DemoRecord record{word(p),word(p+4),std::bit_cast<float>(word(p+8)),
                          std::bit_cast<float>(word(p+12)),word(p+16)};
        if(!std::isfinite(record.x) || !std::isfinite(record.y) || record.direction>=24 ||
           (i && record.milliseconds<records.back().milliseconds)) {
            error="Invalid demo record";return false;
        }
        records.push_back(record);
    }
    out=std::move(records);error.clear();return true;
}
struct DemoFrame {
    std::optional<DemoRecord> pose;
    uint32_t flags=0;
    std::optional<unsigned> weaponSlot; // native direct selection bits16..24
    bool ended=false;
    bool fire() const { return (flags&2)!=0; }
};
class DemoReplay {
public:
    bool load(std::span<const uint8_t> bytes,std::string& error) {
        std::vector<DemoRecord> records;
        if(!decodeDemoReplay(bytes,records,error))return false;
        records_=std::move(records);reset();return true;
    }
    void reset() { next_=0;elapsedMs_=0;ended_=false;pose_.reset(); }
    bool empty() const { return records_.empty(); }
    size_t recordCount() const { return records_.size(); }
    double durationMilliseconds() const {
        return empty()?0:records_.back().milliseconds-records_.front().milliseconds;
    }
    // 429110 starts at the first record's clock. 4291d0 consumes one due record
    // per native update; the fixed-host-cadence adapter consumes all due records
    // to retain recorded real-time duration. It does not interpolate poses.
    // Direct weapon commands are retained even when several records fall in one
    // host tick; the most recent command wins, as in sequential native updates.
    DemoFrame advance(double milliseconds) {
        DemoFrame result;
        if(empty() || ended_) { result.pose=pose_;result.ended=true;return result; }
        if(!std::isfinite(milliseconds) || milliseconds<0)milliseconds=0;
        elapsedMs_+=milliseconds;
        const double clock=records_.front().milliseconds+elapsedMs_;
        while(next_<records_.size() && records_[next_].milliseconds<=clock) {
            const auto& record=records_[next_++];
            pose_=record;result.flags=record.flags;
            for(unsigned slot=0;slot<9;++slot)if(record.flags&(0x10000u<<slot)) {
                result.weaponSlot=slot;break;
            }
            if(record.flags&0x8000u) { ended_=true;break; }
        }
        if(next_==records_.size())ended_=true;
        result.pose=pose_;result.ended=ended_;return result;
    }
private:
    std::vector<DemoRecord> records_;
    size_t next_=0;
    double elapsedMs_=0;
    bool ended_=false;
    std::optional<DemoRecord> pose_;
};
} // namespace vyruz
