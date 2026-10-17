#pragma once
#include "render/viewport.h"
#include <array>
#include <cmath>
#include <string_view>

namespace vyruz {
struct HudAspect { int numerator,denominator;std::string_view name; };
inline constexpr std::array<HudAspect,12> HudAspects{{
    {0,0,"Auto"},{1,1,"1:1"},{4,3,"4:3"},{5,4,"5:4"},{3,2,"3:2"},
    {16,10,"16:10"},{16,9,"16:9"},{17,9,"17:9"},{18,9,"18:9"},
    {20,9,"20:9"},{21,9,"21:9"},{32,9,"32:9"}
}};
// HUD-only frame. The original world projection/camera and menu coordinates
// remain untouched. Artwork scales uniformly; anchors use the frame edges.
struct HudLayout {
    float x=0,y=0,width=640,height=480,scale=1;
    static HudLayout fit(Viewport view,int mode) {
        if(mode<=0 || mode>=int(HudAspects.size()))return {0,0,view.width,view.height,1};
        const auto aspect=HudAspects[mode];
        const float ratio=float(aspect.numerator)/aspect.denominator;
        const float width=std::max(640.f,480*ratio),height=std::max(480.f,640/ratio);
        const float scale=std::min(view.width/width,view.height/height);
        return {(view.width-width*scale)/2,(view.height-height*scale)/2,width,height,scale};
    }
};
}
