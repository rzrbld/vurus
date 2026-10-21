#pragma once
#include "game/properties.h"
namespace vyruz {
// Derived authored input only: keep all query admission decisions dynamic.
// Retaining the source makes ordinary SharedProperties writes detach, so a
// recycled row or live property edit cannot reuse a stale classification.
class NavigationItemCache {
public:
    bool resolve(const SharedProperties& properties) {
        if(&source_.read()==&properties.read())return !properties.empty()&&onlyForPlayer_;
        const auto* item=attribute(properties,"item");
        const bool value=item&&propertyBool(item->children,"Only-for-player");
        source_=properties;onlyForPlayer_=value;
        return value;
    }
private:
    SharedProperties source_;
    // Empty properties have no Item, including an empty source after a move.
    bool onlyForPlayer_=false;
};
}
