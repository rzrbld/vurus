#pragma once
#include <algorithm>
#include <cstddef>
#include <optional>
#include <vector>

namespace vyruz {
// Conservative role membership, not live/available/nearby admission. Keep IDs
// sorted and never erase during a collection pass; death and delay stay dynamic.
class PickupCandidateIndex {
    std::vector<size_t> ids_;
public:
    struct Cursor { size_t offset=0,minimum=0; };
    void clear() { ids_.clear(); }
    void include(size_t id) {
        const auto at=std::lower_bound(ids_.begin(),ids_.end(),id);
        if(at==ids_.end() || *at!=id)ids_.insert(at,id);
    }
    std::optional<size_t> next(Cursor& cursor) const {
        // A callback can insert below the cursor and shift previously visited
        // IDs to its offset. Resume at the next numeric slot, like the original
        // full-table loop. Newly appended/higher IDs remain visible this pass.
        if(cursor.offset<ids_.size() && ids_[cursor.offset]<cursor.minimum)
            cursor.offset=std::lower_bound(ids_.begin()+cursor.offset,ids_.end(),cursor.minimum)-ids_.begin();
        if(cursor.offset==ids_.size())return {};
        const auto id=ids_[cursor.offset++];cursor.minimum=id+1;return id;
    }
};
}
