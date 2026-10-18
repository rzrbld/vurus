#pragma once
#include "game/properties.h"
#include <unordered_map>

namespace vyruz {
// Conservative preload closure, not a gameplay spawn whitelist. Lazy loading
// remains available. Scan every nested authored value/argument so HAVE/RANDOM,
// CREATE, generator Product, weapons, directional hit FX and placement overrides
// all participate without maintaining a second partial property interpreter.
inline std::vector<bool> reachableSpriteClasses(const SceneContent& scene) {
    std::unordered_map<std::string,size_t> names;
    for(size_t i=0;i<scene.classes.size();++i)names.emplace(folded(scene.classes[i].name),i);
    std::vector<bool> seen(scene.classes.size());std::vector<size_t> pending;
    const auto add=[&](size_t i){if(i<seen.size()&&!seen[i]){seen[i]=true;pending.push_back(i);}};
    const auto name=[&](const std::string& value){
        const auto it=names.find(folded(value));if(it!=names.end())add(it->second);
    };
    const auto scan=[&](auto&& self,const std::vector<TextNode>& nodes)->void{
        for(const auto& node:nodes){
            for(const auto& value:node.values)name(value);
            for(const auto& argument:node.arguments)name(argument);
            self(self,node.children);
        }
    };
    for(const auto& object:scene.objects){add(object.classId.value);scan(scan,object.properties);}
    // Inventory/weapon carry and debug selection can introduce these without
    // a placed instance. Keep their complete transitive products preloaded.
    for(size_t i=0;i<scene.classes.size();++i){const auto& p=scene.classes[i].properties;
        if(attribute(p,"weapon")||attribute(p,"item")||attribute(p,"ammo"))add(i);
    }
    // Runtime-selected class names not represented by authored references.
    for(const char* value:{"Water-Splash","Bullet-Ground-Impact","Explosion","Shock-Wave",
        "Flipper-Bong-Up-Left","Flipper-Bong-Up-Right","Flipper-Bong-Down-Left","Flipper-Bong-Down-Right",
        "No-Inertia-Control-FX-1","Invulnerability-FX-1","Invulnerability-FX-2","Invulnerability-FX-3",
        "Invulnerability-FX-4","Invulnerability-FX-5","Touch-And-Destroy-FX-1",
        "Touch-And-Destroy-FX-2","Touch-And-Destroy-FX-3"})name(value);
    for(size_t cursor=0;cursor<pending.size();++cursor)scan(scan,scene.classes[pending[cursor]].properties);
    return seen;
}
}
