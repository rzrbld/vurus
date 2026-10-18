#pragma once
#include "assets/gaf_sprite.h"
#include "game/properties.h"
#include <algorithm>
#include <map>
#include <set>

namespace vyruz {
inline std::string spriteBankKey(std::string path) {
    path=folded(path);std::replace(path.begin(),path.end(),'\\','/');return path;
}
// Union of every possible consumer, not only current cursors/visible objects.
// Unknown controllers and vehicle banks deliberately keep every bitmap.
struct SpriteSequenceUse {
    bool all=false;
    std::set<uint32_t> sequences,directionalBases;
    std::vector<bool> select(const GafAnimation& bank) const {
        std::vector<bool> result(bank.sequences.size(),all);
        const auto add=[&](uint32_t n){if(n && n<=result.size())result[n-1]=true;};
        for(auto n:sequences)add(n);
        // Match the generic renderer's conditional 24-direction bank rule.
        for(auto base:directionalBases)if(base && uint64_t(base)+23<=result.size())
            for(uint32_t n=0;n<24;++n)add(base+n);
        return result;
    }
};
using SpriteSequenceUses=std::map<std::string,SpriteSequenceUse>;
inline SpriteSequenceUses sceneSpriteSequenceUses(const SceneContent& scene) {
    SpriteSequenceUses uses;
    const auto object=[&](const auto& source,bool player){
        const auto family=objectSubtype(source.properties,source.sprite);
        // These ordinary banks use authored sequences or the generic direction
        // window. All special controllers (including gates/lifts) stay full.
        const bool ordinary=family=="fx" || family=="thing" || family=="item" || family=="ammo";
        for(const auto* ref:{&source.sprite,&source.shadow})if(!ref->path.empty()) {
            auto& use=uses[spriteBankKey(ref->path)];
            use.all|=player || !ordinary || attribute(source.properties,"gate") || attribute(source.properties,"lift");
            if(ref->sequence.value){use.sequences.insert(ref->sequence.value);use.directionalBases.insert(ref->sequence.value);}
        }
    };
    // Include even unplaced classes: carried equipment, generators, CREATE,
    // debug selection and restored births cannot introduce an unplanned pose.
    // Class reachability still controls which *banks* are actually loaded.
    for(const auto& cls:scene.classes)object(cls,false);
    for(const auto& placed:scene.objects)object(placed,scene.player && placed.id==*scene.player);
    uses["common/data/object/fx/time-warp.gaf"].sequences.insert(1);
    return uses;
}
}
