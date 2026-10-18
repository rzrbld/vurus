#include "render/loading_screen.h"
#include <algorithm>
#include <cmath>

namespace vyruz {
bool LoadingScreen::load(const ContentReader& read,std::string& error) {
    if(!animation_.frames.empty())return true;
    std::vector<uint8_t> bytes;
    GafAnimation decoded;
    if(!read("COMMON/DATA/MENU/LOADSCREEN.GAF",bytes) ||
       !decodeGafAnimation(bytes,decoded,GafDecodeMode::SequenceFirstPoses) || decoded.sequences.size()!=5) {
        error="missing or invalid loading resource: COMMON/DATA/MENU/LOADSCREEN.GAF";
        return false;
    }
    for(const auto& sequence:decoded.sequences)
        if(sequence.subframes.empty() || sequence.subframes.front().frameIndex>=decoded.frames.size()) {
            error="invalid loading resource sequence";return false;
        }
    animation_=std::move(decoded);
    textures_.resize(animation_.frames.size());
    return true;
}
void LoadingScreen::begin(bool cycle) {
    if(begun_ && cycle)picture_=(picture_+1)%4;
    begun_=true;
}
void LoadingScreen::draw(Gfx& gfx,size_t sequence,float fraction) {
    if(sequence>=animation_.sequences.size())return;
    const auto& pose=animation_.sequences[sequence].subframes.front();
    const auto& frame=animation_.frames[pose.frameIndex];
    const auto& image=frame.image;
    const float width=std::floor(image.width*fraction),height=image.height;
    if(width<=0 || height<=0)return;
    auto& texture=textures_[pose.frameIndex];
    if(!texture)texture=gfx.uploadTexture(image);
    // Pictures are direct centered bitmap copies; the final sequence uses its
    // authored origin, just like native 404760 + 45eab0.
    const float x=sequence<4 ? (640-image.width)/2.f : frame.originX+pose.offsetX;
    const float y=sequence<4 ? (480-image.height)/2.f : frame.originY+pose.offsetY;
    const float u=width/image.width;
    gfx.beginQuads(texture);
    gfx.quad(x,y,0,0,x+width,y,u,0,x+width,y+height,u,1,x,y+height,0,1);
    gfx.endQuads();
}
void LoadingScreen::progress(Gfx& gfx,float fraction) {
    if(animation_.sequences.empty())return;
    if(!std::isfinite(fraction))fraction=0;
    gfx.beginFrame(640,480);
    gfx.clear(0,0,0);
    glColor4f(1,1,1,1);
    glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    const auto view=gfx.viewport();glPushMatrix();glTranslatef(view.uiX(),view.uiY(),0);
    draw(gfx,picture_,1);
    draw(gfx,animation_.sequences.size()-1,std::clamp(fraction,0.f,1.f));
    glPopMatrix();gfx.present();
}
void LoadingScreen::clear(Gfx& gfx) {
    for(auto texture:textures_)if(texture)gfx.deleteTexture(texture);
    textures_.clear();animation_={};picture_=0;begun_=false;
}
}
