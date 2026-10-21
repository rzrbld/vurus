#pragma once
#include "game/content.h"
#include "game/content_names.h"
#include <unordered_map>
namespace vyruz {
// Derived lookup for an authored catalog. Keep the first case-insensitive
// match, including duplicates and empty names, exactly as findClass does.
// Call clear after in-place name/order edits; relocation/growth is detected.
class ClassNameIndex {
    struct Hash {
        using is_transparent=void;
        size_t operator()(std::string_view name) const {
            size_t hash=2166136261u;
            for(unsigned char c:name)hash=(hash^size_t(std::tolower(c)))*16777619u;
            return hash;
        }
    };
    struct Equal {
        using is_transparent=void;
        bool operator()(std::string_view a,std::string_view b) const { return contentNameEqual(a,b); }
    };
    std::unordered_map<std::string,size_t,Hash,Equal> names_;
    const ObjectClass* source_=nullptr;
    size_t count_=0;
public:
    void clear() { names_.clear();source_=nullptr;count_=0; }
    const ObjectClass* find(const std::vector<ObjectClass>& classes,std::string_view name) {
        // A moved-from index can retain its source pointer but has no entries.
        if(source_!=classes.data()||count_!=classes.size()||(!classes.empty()&&names_.empty())) {
            clear();names_.reserve(classes.size());
            for(size_t i=0;i<classes.size();++i)names_.emplace(classes[i].name,i);
            source_=classes.data();count_=classes.size();
        }
        const auto found=names_.find(name);
        return found==names_.end()?nullptr:&classes[found->second];
    }
};
}
