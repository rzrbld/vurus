#pragma once
#include <algorithm>
#include <cmath>
namespace vyruz {
// Logical SDL window coordinates and framebuffer pixels are deliberately
// independent. Keep the original HUD in a centered 640x480 safe area.
struct Viewport {
    float width=640,height=480;
    // Align the shared camera, not individual objects: fractional sampling of
    // baked terrain and its alpha overlays can expose alternating edge texels.
    // Convert through drawable pixels so high-DPI/aspect layouts stay intact.
    static float pixelAligned(float position,float extent,int pixels) {
        const float scale=std::max(1,pixels)/extent;
        return std::round(position*scale)/scale;
    }
    float uiX() const {return (width-640)/2;}
    float uiY() const {return (height-480)/2;}
    static Viewport fit(int w,int h) {
        const float aspect=float(std::max(1,w))/std::max(1,h);
        return {std::max(640.f,480*aspect),std::max(480.f,640/aspect)};
    }
    float x(float windowX,int windowWidth) const{return windowX*width/std::max(1,windowWidth);}
    float y(float windowY,int windowHeight) const{return windowY*height/std::max(1,windowHeight);}
};
}
