#pragma once
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace vyruz {
// Native 413200 -> 4132e0 -> 4133a0. Records suppress duplicate sample handles
// until expiry; each gets exactly one launch attempt, even if cooldown rejects it.
// Names are the port's stable identity for original loaded sample handles.
class VoiceSchedule {
public:
    using Time=uint64_t;
    bool enqueue(std::string name,Time now,Time delay,Time hold,Time tail) {
        if(name.empty() || std::any_of(events_.begin(),events_.end(),
            [&](const Event& e){return e.name==name;}))return false;
        events_.push_back({std::move(name),now+delay,hold,now+delay+hold+tail,false});
        return true;
    }
    std::vector<std::string> advance(Time now) {
        std::vector<std::string> played;
        // Native append order is significant when several samples become due.
        for(auto& e:events_) {
            if(!e.attempted && e.ready<=now) {
                if(nextAllowed_==0 || nextAllowed_<=now) {
                    nextAllowed_=now+e.hold;played.push_back(e.name);
                }
                e.attempted=true;
            }
        }
        // Deliberately after launch attempts: native also attempts a late event
        // on the update that removes its expired duplicate-suppression record.
        std::erase_if(events_,[&](const Event& e){return e.expiry<=now;});
        return played;
    }
    // Native411170 clears pending/dedup records, not global5235a0 or the sample
    // already playing on voice channel5. Audio playback is the caller's concern.
    void clear() {events_.clear();}
    size_t size() const {return events_.size();}
    Time nextAllowed() const {return nextAllowed_;}
private:
    struct Event {std::string name;Time ready,hold,expiry;bool attempted;};
    std::vector<Event> events_;
    Time nextAllowed_=0;
};
}
