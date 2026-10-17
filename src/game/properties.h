#pragma once
#include "game/content.h"
#include "game/content_names.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <string_view>

namespace vyruz {
inline std::string folded(std::string value) {
    for (auto& c:value) c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return value;
}
inline std::string propertyText(const std::vector<TextNode>& nodes,const std::string& name,
                                std::string fallback={}) {
    const auto* node=contentProperty(nodes,name);
    if (!node || node->values.empty()) return fallback;
    return node->values.front();
}
inline float propertyNumber(const std::vector<TextNode>& nodes,const std::string& name,float fallback=0) {
    const auto text=propertyText(nodes,name);
    if (text.empty()) return fallback;
    char* end=nullptr;
    const float value=std::strtof(text.c_str(),&end);
    return end!=text.c_str() && (*end=='\0' || (*end==';' && end[1]=='\0')) && std::isfinite(value)?value:fallback;
}
inline bool propertyBool(const std::vector<TextNode>& nodes,const std::string& name,bool fallback=false) {
    auto text=folded(propertyText(nodes,name));
    // Shipped assignments include a terminal semicolon (e.g. the laser's
    // At-current-position = 1;), also accepted by propertyNumber above.
    if(!text.empty() && text.back()==';')text.pop_back();
    if (text=="true" || text=="yes" || text=="1") return true;
    if (text=="false" || text=="no" || text=="0") return false;
    return fallback;
}
inline const TextNode* attribute(const std::vector<TextNode>& nodes,std::string_view kind) {
    for (const auto& node:nodes) if (contentNameIs(node.key,"type") && !node.arguments.empty() &&
        contentNameIs(node.arguments.front(),kind)) return &node;
    return nullptr;
}
//40adf4 and40bb93: Item Timeout can set Managed between explicit setters.
inline bool nativeClassManaged(const std::vector<TextNode>& properties) {
    bool managed=false;
    for(const auto& node:properties) {
        if(contentNameIs(node.key,"managed"))
            managed=propertyBool(std::vector<TextNode>{node},"Managed");
        else if(contentNameIs(node.key,"type") && !node.arguments.empty() &&
                contentNameIs(node.arguments.front(),"item"))
            for(const auto& field:node.children)
                if(contentNameIs(field.key,"timeout") &&
                   propertyNumber(std::vector<TextNode>{field},"Timeout")!=0)managed=true;
    }
    return managed;
}
inline const ObjectClass* findClass(const SceneContent& scene,const std::string& name) {
    for (const auto& cls:scene.classes) if (contentNameEqual(cls.name,name)) return &cls;
    return nullptr;
}
// Native 413820: explicit subtype overrides the primary GAF's parent folder.
inline std::string objectSubtype(const std::vector<TextNode>& properties,const SpriteReference& sprite) {
    auto kind=folded(propertyText(properties,"Sub-type"));
    if(!kind.empty())return kind;
    auto path=folded(sprite.path);std::replace(path.begin(),path.end(),'\\','/');
    const auto end=path.rfind('/');if(end==std::string::npos||!end)return {};
    const auto start=path.rfind('/',end-1);
    return path.substr(start==std::string::npos?0:start+1,end-(start==std::string::npos?0:start+1));
}
inline std::string objectSubtype(const ObjectPlacement& object) {
    return objectSubtype(object.properties,object.sprite);
}
// Compile the immutable placement subtype once at construction/restore. Keep
// the explicit subtype / primary GAF / empty-name attribute fallback ordering.
inline uint32_t nativeObjectSubtype(const ObjectPlacement& object,bool player=false) {
    const auto name=objectSubtype(object);
    constexpr std::string_view names[]={"ammo","fx","item","lift","gate","thing","cop","staticgen",
        "vyruz","walker","burner","jeep","roller","heli","turret","spider","tank"};
    for(uint32_t n=0;n<std::size(names);++n)if(name==names[n])return 0x10+n;
    if(name=="player"||player)return 0x18;
    if(name.empty())for(const auto [kind,type]:{std::pair{"ammo",0x10u},{"fx",0x11u},{"item",0x12u},{"lift",0x13u},{"gate",0x14u}})
        if(attribute(object.properties,kind))return type;
    return 0;
}
inline bool hasBehaviour(const ObjectPlacement& object,std::string_view name) {
    for (const auto& b:object.behaviours) if (contentNameIs(b,name)) return true;
    return false;
}
}
