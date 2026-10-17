#include "game/hud_stats.h"
#include "game/properties.h"
#include <cmath>

namespace vyruz {
int nativeSpeedReadout(float magnitude) {
    if(!std::isfinite(magnitude))return 0; // Invalid port state, not native parity.
    if(magnitude>=1000)magnitude=999;
    else if(magnitude<=-1000)magnitude=-999;
    // Native x87 multiplication retains precision until __ftol truncates.
    return static_cast<int>(static_cast<double>(magnitude)*100.0)%1000;
}

namespace {
std::string fullValue(const std::vector<TextNode>& nodes,const char* key) {
    const auto* n=contentProperty(nodes,key);
    std::string value;
    if(n) for(const auto& word:n->values) {if(!value.empty())value+=' ';value+=word;}
    return value;
}
bool behavior(const std::vector<std::string>& list,const char* name) {
    for(const auto& b:list) if(folded(b)==name)return true;
    return false;
}
bool targetingPlayer(const std::vector<TextNode>& properties,
                     const std::vector<std::string>& behaviors,const SceneContent& scene) {
    if(behavior(behaviors,"player"))return false;
    // Test both active tracking channels; an unrelated inactive TYPE block
    // inherited from defaults must not make neutral scenery an enemy.
    for(const auto* mode:{"seeker","tracker"}) {
        if(!behavior(behaviors,mode))continue;
        const auto* a=attribute(properties,mode);
        const auto* target=a?findClass(scene,fullValue(a->children,"Target")):nullptr;
        if(target&&behavior(target->behaviours,"player"))return true;
    }
    return false;
}
std::string kind(const ObjectPlacement& object) {
    const auto override=folded(propertyText(object.properties,"Sub-type"));
    if(!override.empty())return override;
    std::string path=folded(object.sprite.path);
    std::replace(path.begin(),path.end(),'\\','/');
    const auto last=path.rfind('/');
    if(last==std::string::npos||last==0)return {};
    const auto prev=path.rfind('/',last-1);
    const auto begin=prev==std::string::npos?0:prev+1;
    return path.substr(begin,last-begin);
}
}
bool countsAsEnemy(const ObjectPlacement& object,const SceneContent& scene) {
    if(!killCreditEligible(object))return false;
    // Native StaticGen counts the generator itself if its product is hostile,
    // independent of activation, birth limit, or product quantity.
    if(kind(object)!="staticgen")return targetingPlayer(object.properties,object.behaviours,scene);
    for(const auto& node:object.properties) {
        if(folded(node.key)!="type"||node.arguments.empty()||folded(node.arguments[0])!="generator")continue;
        const auto* product=findClass(scene,fullValue(node.children,"Product"));
        if(product&&targetingPlayer(product->properties,product->behaviours,scene))return true;
    }
    return false;
}
bool killCreditEligible(const ObjectPlacement& object) {
    const auto type=kind(object);
    for(const auto* name:{"cop","staticgen","vyruz","walker","burner","jeep","roller","heli","turret","spider","tank"})
        if(type==name)return true;
    return false;
}
}
