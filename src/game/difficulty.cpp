#include "game/difficulty.h"
#include "game/properties.h"
#include <array>
namespace vyruz {
bool loadDifficultyPolicy(const SceneContent& scene, int level, DifficultyPolicy& out, std::string& error) {
    if(level<0 || level>3){error="Invalid difficulty level";return false;}
    std::array<DifficultyPolicy,4> policies;
    bool found=false;
    for(const auto& document:scene.classDocuments) {
        int selected=-1;
        for(const auto& node:document.nodes) {
            const auto key=folded(node.key);
            if(key=="difficulty-level") {
                if(node.values.empty()){error="Missing difficulty level";return false;}
                char* end=nullptr;const auto value=std::strtol(node.values.front().c_str(),&end,10);
                if(end==node.values.front().c_str() || *end || value<0 || value>3){error="Invalid authored difficulty level";return false;}
                selected=int(value);found=true;continue;
            }
            if(selected<0)continue;
            auto& p=policies[size_t(selected)];float* target=nullptr;
            if(key=="player-shield-max")target=&p.playerShieldMax;
            else if(key=="player-nrj-max")target=&p.playerEnergyMax;
            else if(key=="nmy-scan-delay-ratio")target=&p.enemyScanDelay;
            else if(key=="nmy-scan-range-ratio")target=&p.enemyScanRange;
            else if(key=="nmy-shield-up-step-ratio")target=&p.enemyShieldRegeneration;
            else if(key=="player-shield-up-step-ratio")target=&p.playerShieldRegeneration;
            else if(key=="mine-random-count-ratio")target=&p.mineRandomCount;
            else if(key=="nmy-fire-delay-ratio")target=&p.enemyFireDelay;
            else if(key=="shield-up-item-value-ratio")target=&p.shieldItemValue;
            else if(key=="nrj-up-item-value-ratio")target=&p.energyItemValue;
            if(!target)continue;
            if(node.values.empty()){error="Missing difficulty value: "+node.key;return false;}
            char* end=nullptr;const float value=std::strtof(node.values.front().c_str(),&end);
            if(end==node.values.front().c_str() || *end || !std::isfinite(value) || value<0 || value>1000000 ||
               (value==0 && target!=&p.mineRandomCount)){error="Invalid difficulty value: "+node.key;return false;}
            *target=value;
        }
    }
    DifficultyPolicy selected=found?policies[size_t(level)]:DifficultyPolicy{};
    selected.level=level;out=selected;error.clear();return true;
}
}
