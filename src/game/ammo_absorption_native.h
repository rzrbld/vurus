#pragma once
#include "game/properties.h"
#include <span>

namespace vyruz {
// 43d350 ->43d780: only the later direct Ammo component uses Absorption.
// Native class TYPE nodes are prepended, then413550 reverses the list during
// actor construction: first authored matching instance attribute wins.
// Shipped Class-name references use unique canonical class names, so the
// resolved source name identifies the same original class pointer.
inline float nativeAbsorbedAmmoDamage(std::span<const TextNode> targetProperties,
                                     std::string_view sourceAmmo,float damage) {
    for(const auto& node:targetProperties) {
        if(!contentNameIs(node.key,"type") || node.arguments.empty() ||
           !contentNameIs(node.arguments.front(),"absorption"))continue;
        const auto* name=contentProperty(node.children,"Class-name");
        if(name && !name->values.empty() && contentNameEqual(name->values.front(),sourceAmmo))
            return damage*propertyNumber(node.children,"Coeff");
    }
    return damage;
}
}
