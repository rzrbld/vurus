#pragma once
#include "game/properties.h"
#include <optional>
#include <string>

namespace vyruz {
// 43e5f0 recognizes a leading '*' as end-of-game, not a scenery filename.
inline bool endingDestination(std::string_view path) { return !path.empty() && path.front()=='*'; }
//446a8a retains the living Player body when both Destination fields are
//nonnegative. Lift attributes default to0,0 (405210 clears the block); World's
//startup -1 sentinels are overwritten on departure. These are not arrival XY.
inline bool liftRetainsPlayerBody(const std::vector<TextNode>& fields) {
    const auto* node=contentProperty(fields,"Destination");
    if(!node)return true;
    if(node->values.size()<2)return false;
    const auto nonnegative=[](const std::string& text) {
        char* end=nullptr;const float value=std::strtof(text.c_str(),&end);
        return end!=text.c_str()&&(*end=='\0'||(*end==';'&&end[1]=='\0'))&&std::isfinite(value)&&value>=0;
    };
    return nonnegative(node->values[0])&&nonnegative(node->values[1]);
}
// Native442bf0 changes scenery only when the Lift names one. Empty Scenery
// means another level in the current scenery, not an incomplete destination.
inline std::optional<std::string> liftDestination(const std::vector<TextNode>& fields,
                                                   const std::string& currentMap) {
    auto scenery=propertyText(fields,"Scenery"),file=propertyText(fields,"3do-file");
    if(file.empty())return {};
    if(endingDestination(file))return file;
    if(scenery.empty()) {
        const auto end=currentMap.find_last_of("/\\");
        if(end==std::string::npos)return {};
        scenery=currentMap.substr(0,end);
    }
    if(scenery.empty() || scenery==".." || file==".." ||
       scenery.find_first_of("/\\:")!=std::string::npos || file.find_first_of("/\\:")!=std::string::npos)return {};
    if(const auto dot=file.find_last_of('.');dot!=std::string::npos)file.resize(dot);
    if(file.empty() || file=="." || file=="..")return {};
    return scenery+"/"+file+".3DG";
}
// Applied to the staged departing snapshot; the caller publishes it only after
// destination loading succeeds. Failed loads must not spend a Lift use.
inline bool consumeLiftUse(ObjectPlacement& object) {
    for(auto& node:object.properties) {
        if(!contentNameIs(node.key,"type") || node.arguments.empty() || !contentNameIs(node.arguments[0],"lift"))continue;
        const float uses=propertyNumber(node.children,"Usage-Count");
        if(uses<1)return false;
        const auto next=std::to_string(static_cast<int64_t>(std::min(double(uses),double(INT32_MAX)))-1);
        for(auto i=node.children.rbegin();i!=node.children.rend();++i)if(contentNameIs(i->key,"usage-count")) {
            i->values={next};return true;
        }
    }
    return false;
}
}
