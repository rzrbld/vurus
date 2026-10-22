#pragma once
#include "assets/gaf_sprite.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace vyruz {
// Separate extrema preserve the draw path's floating addition order. Combining
// offset+origin at build time could round differently at a viewport boundary.
struct SpriteSequenceEnvelope {
    bool ready=false,valid=false,drawable=false;
    float minOffsetX=0,maxOffsetX=0,minOffsetY=0,maxOffsetY=0;
    float minOriginX=0,maxOriginX=0,minOriginY=0,maxOriginY=0;
    float width=0,height=0;
    bool outside(float x,float y,float cameraX,float cameraY,float viewWidth,float viewHeight,float altitude=0) const {
        if(!valid || !std::isfinite(altitude) || !std::isfinite(x) || !std::isfinite(y) ||
           !std::isfinite(cameraX) || !std::isfinite(cameraY) ||
           !std::isfinite(viewWidth) || !std::isfinite(viewHeight) || viewWidth<=0 || viewHeight<=0)return false;
        if(!drawable)return true;
        const float left=x+0.f+minOffsetX+minOriginX;
        const float right=x+0.f+maxOffsetX+maxOriginX+width;
        const float top=y+0.f-altitude+minOffsetY+minOriginY;
        const float bottom=y+0.f-altitude+maxOffsetY+maxOriginY+height;
        const float viewRight=cameraX+viewWidth,viewBottom=cameraY+viewHeight;
        if(!std::isfinite(left)||!std::isfinite(right)||!std::isfinite(top)||!std::isfinite(bottom)||
           !std::isfinite(viewRight)||!std::isfinite(viewBottom))return false;
        return right<=cameraX || left>=viewRight || bottom<=cameraY || top>=viewBottom;
    }
};
inline SpriteSequenceEnvelope spriteSequenceEnvelope(const GafAnimation& animation,const GafSequence& sequence) {
    SpriteSequenceEnvelope e;e.ready=e.valid=true;
    for(const auto& pose:sequence.subframes){
        if(pose.frameIndex==GafSubframe::Blank)continue;
        if(pose.frameIndex>=animation.frames.size()){e.valid=false;return e;}
        const auto& frame=animation.frames[pose.frameIndex];
        if(frame.image.width<=0 || frame.image.height<=0)continue;
        const bool flipX=pose.flags&0x100,flipY=pose.flags&0x200;
        if((flipX&&pose.offsetX==std::numeric_limits<int>::min()) ||
           (flipY&&pose.offsetY==std::numeric_limits<int>::min())){e.valid=false;return e;}
        const float ox=flipX?-pose.offsetX:pose.offsetX,oy=flipY?-pose.offsetY:pose.offsetY;
        const float ix=flipX?frame.flippedOriginX:frame.originX,iy=flipY?frame.flippedOriginY:frame.originY;
        if(!e.drawable){e.minOffsetX=e.maxOffsetX=ox;e.minOffsetY=e.maxOffsetY=oy;
            e.minOriginX=e.maxOriginX=ix;e.minOriginY=e.maxOriginY=iy;e.drawable=true;}
        e.minOffsetX=std::min(e.minOffsetX,ox);e.maxOffsetX=std::max(e.maxOffsetX,ox);
        e.minOffsetY=std::min(e.minOffsetY,oy);e.maxOffsetY=std::max(e.maxOffsetY,oy);
        e.minOriginX=std::min(e.minOriginX,ix);e.maxOriginX=std::max(e.maxOriginX,ix);
        e.minOriginY=std::min(e.minOriginY,iy);e.maxOriginY=std::max(e.maxOriginY,iy);
        e.width=std::max(e.width,float(frame.image.width));e.height=std::max(e.height,float(frame.image.height));
    }
    return e;
}
}
