#include "render/gfx.h"
#include "render/sprite_filter.h"

#include <cstdio>
#include <cstring>
#include <algorithm>
#include <cmath>

namespace vyruz {

bool Gfx::init(SDL_Window* window) {
    window_ = window;
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
    ctx_ = SDL_GL_CreateContext(window_);
    if (!ctx_) {
        std::fprintf(stderr, "SDL_GL_CreateContext failed: %s\n", SDL_GetError());
        return false;
    }
    const char* driver=SDL_GetCurrentVideoDriver();
    // Offscreen Mesa accepts interval 1 but has no display retrace to wait for.
    const bool headless=driver && (!std::strcmp(driver,"offscreen") || !std::strcmp(driver,"dummy"));
    vsync_ = !headless && SDL_GL_SetSwapInterval(1)==0 && SDL_GL_GetSwapInterval()==1;
    if(!vsync_)SDL_GL_SetSwapInterval(0);
    std::printf("frame pacing: %s\n",vsync_?"VSync":"software deadline pacing (VSync unavailable)");
    SDL_GetWindowSize(window_, &winW_, &winH_);

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glShadeModel(GL_SMOOTH);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

    const char* ver = reinterpret_cast<const char*>(glGetString(GL_VERSION));
    std::printf("OpenGL %s\n", ver ? ver : "(unknown)");
    const char* renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    const char* vendor = reinterpret_cast<const char*>(glGetString(GL_VENDOR));
    std::printf("OpenGL renderer: %s (%s)\n", renderer ? renderer : "unknown",
                vendor ? vendor : "unknown");
    return true;
}

void Gfx::shutdown() {
    crt_.shutdown();sceneEffects_.shutdown();smaa_.shutdown();
    for(const auto& [id,sprite]:spriteTextures_)if(sprite.texture)glDeleteTextures(1,&sprite.texture);
    spriteTextures_.clear();
    deleteTexture(distortion_);distortion_=0;distortionW_=distortionH_=0;
    deleteTexture(brightness_);brightness_=0;brightnessW_=brightnessH_=0;
    if (ctx_) {
        SDL_GL_DeleteContext(ctx_);
        ctx_ = nullptr;
    }
}

void Gfx::beginFrame(int vw, int vh) {
    vw_ = vw;
    vh_ = vh;
    SDL_GetWindowSize(window_, &winW_, &winH_);
    SDL_GL_GetDrawableSize(window_, &drawableW_, &drawableH_);
    glViewport(0, 0, drawableW_, drawableH_);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    // Top-left origin, y increases downward.
    const auto view=viewport();
    glOrtho(0.0, view.width, view.height, 0.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

void Gfx::clear(float r, float g, float b) {
    glClearColor(r, g, b, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
}

GLuint Gfx::uploadTexture(const uint8_t* rgba, int w, int h,bool sprite) {
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    current_ = tex;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    if(sprite)spriteTextures_[tex]={w,h};
    return tex;
}

void Gfx::deleteTexture(GLuint tex) {
    if(auto found=spriteTextures_.find(tex);found!=spriteTextures_.end()) {
        if(found->second.texture){glDeleteTextures(1,&found->second.texture);if(current_==found->second.texture)current_=0;}
        spriteTextures_.erase(found);
    }
    if (tex) glDeleteTextures(1, &tex);
    if (current_ == tex) current_ = 0;
}

GLuint Gfx::filteredTexture(GLuint original) {
    const auto found=spriteTextures_.find(original);
    if(found==spriteTextures_.end())return original;
    auto& sprite=found->second;
    if(!sprite.texture) {
        // Created only on demand with filtering enabled; Off retains the exact
        // original texture and avoids mipmap allocation or image processing.
        glBindTexture(GL_TEXTURE_2D,original);
        std::vector<uint8_t> pixels(size_t(sprite.width)*sprite.height*4);
        glPixelStorei(GL_PACK_ALIGNMENT,1);
        glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
        const auto levels=filteredSpriteMips(pixels.data(),sprite.width,sprite.height);
        glGenTextures(1,&sprite.texture);glBindTexture(GL_TEXTURE_2D,sprite.texture);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        for(size_t level=0;level<levels.size();++level) {
            const auto& mip=levels[level];
            glTexImage2D(GL_TEXTURE_2D,GLint(level),GL_RGBA,mip.width,mip.height,0,GL_RGBA,GL_UNSIGNED_BYTE,mip.rgba.data());
        }
        current_=sprite.texture;
    }
    if(sprite.mode!=spriteFiltering_) {
        glBindTexture(GL_TEXTURE_2D,sprite.texture);current_=sprite.texture;
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,spriteFiltering_==2?GL_LINEAR_MIPMAP_LINEAR:GL_LINEAR);
        sprite.mode=spriteFiltering_;
    }
    return sprite.texture;
}
void Gfx::beginQuads(GLuint tex,bool filterSprite) {
    if(filterSprite && spriteFiltering_!=0)tex=filteredTexture(tex);
    if (tex != current_) {
        glBindTexture(GL_TEXTURE_2D, tex);
        current_ = tex;
    }
    glBegin(GL_QUADS);
}

void Gfx::quad(float x0, float y0, float u0, float v0,
               float x1, float y1, float u1, float v1,
               float x2, float y2, float u2, float v2,
               float x3, float y3, float u3, float v3) {
    glTexCoord2f(u0, v0);
    glVertex2f(x0, y0);
    glTexCoord2f(u1, v1);
    glVertex2f(x1, y1);
    glTexCoord2f(u2, v2);
    glVertex2f(x2, y2);
    glTexCoord2f(u3, v3);
    glVertex2f(x3, y3);
}

void Gfx::endQuads() {
    glEnd();
}

void Gfx::distortScanlines(float phase) {
    // Original 42e9e0/46e080: horizontal row displacement, 16px amplitude,
    // 120 virtual-row sine period. GPU copy avoids CPU framebuffer readback.
    if(!distortion_)glGenTextures(1,&distortion_);
    glBindTexture(GL_TEXTURE_2D,distortion_);current_=distortion_;
    if(distortionW_!=drawableW_ || distortionH_!=drawableH_) {
        distortionW_=drawableW_;distortionH_=drawableH_;
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,drawableW_,drawableH_,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
    }
    glCopyTexSubImage2D(GL_TEXTURE_2D,0,0,0,0,0,drawableW_,drawableH_);
    const auto v=viewport();glDisable(GL_BLEND);glColor4f(1,1,1,1);
    beginQuads(distortion_);
    for(int y=0;y<v.height;++y) {
        const float dx=std::trunc(std::sin((y+phase)*6.28318530718f/120.f)*16.f);
        const float y1=std::min(float(y+1),float(v.height));
        const float a=1.f-y/float(v.height),b=1.f-y1/float(v.height);
        quad(0,y,-dx/v.width,a,v.width,y,1-dx/v.width,a,
             v.width,y1,1-dx/v.width,b,0,y1,-dx/v.width,b);
    }
    endQuads();glEnable(GL_BLEND);
}

void Gfx::applyBrightness() {
    if(gamma_!=50 && drawableW_>0 && drawableH_>0) {
        // Port display preference: y=x+k*x*(1-x), k in[-.5,.5].
        // Black and white stay fixed; derivative is >=.5 at both extremes,
        // so neither end of the slider erases the menu or collapses contrast.
        // A copied RGB texture implements the curve with fixed-function GPU
        // blending, preserving the existing GL compatibility path (no shader,
        // CPU readback, hardware gamma change or proprietary asset processing).
        if(!brightness_)glGenTextures(1,&brightness_);
        glBindTexture(GL_TEXTURE_2D,brightness_);current_=brightness_;
        if(brightnessW_!=drawableW_ || brightnessH_!=drawableH_) {
            brightnessW_=drawableW_;brightnessH_=drawableH_;
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
            // RGB guarantees source alpha1, independently of scene blending.
            glTexImage2D(GL_TEXTURE_2D,0,GL_RGB,drawableW_,drawableH_,0,GL_RGB,GL_UNSIGNED_BYTE,nullptr);
        }
        glCopyTexSubImage2D(GL_TEXTURE_2D,0,0,0,0,0,drawableW_,drawableH_);
        glMatrixMode(GL_MODELVIEW);glPushMatrix();glLoadIdentity();
        glEnable(GL_TEXTURE_2D);glDisable(GL_ALPHA_TEST);glEnable(GL_BLEND);
        const float amount=(std::clamp(gamma_,0,100)-50)*.01f;
        if(amount>0) {
            glBlendFunc(GL_ONE_MINUS_DST_COLOR,GL_ONE);
            glColor4f(amount,amount,amount,1);
        } else {
            glBlendFunc(GL_DST_COLOR,GL_ONE_MINUS_SRC_ALPHA);
            glColor4f(-amount,-amount,-amount,-amount);
        }
        const auto v=viewport();beginQuads(brightness_);
        quad(0,0,0,1,v.width,0,1,1,v.width,v.height,1,0,0,v.height,0,0);
        endQuads();glColor4f(1,1,1,1);
        glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);glPopMatrix();
    }
}

CrtPoint Gfx::mousePoint(float x,float y)const {
    const auto view=viewport();
    if(!crtEnabled())return {view.x(x,winW_),view.y(y,winH_)};
    const auto mapped=CrtGeometry(view.width,view.height).source(x/std::max(1,winW_),1-y/std::max(1,winH_));
    return {mapped.x*view.width,(1-mapped.y)*view.height};
}
void Gfx::applySceneEffects(){
    if(!sceneEffectsEnabled())return;
    const auto view=viewport();std::string error;
    if(!smaa_.draw(drawableW_,drawableH_,view.width,view.height,error)){
        std::fprintf(stderr,"SMAA disabled: %s\n",error.c_str());std::string ignored;smaa_.enable(false,ignored);
    }
    if(!sceneEffects_.draw(drawableW_,drawableH_,view.width,view.height,error)){
        std::fprintf(stderr,"Scene effects disabled for this run: %s\n",error.c_str());
        std::string ignored;sceneEffects_.enable(false,ignored);
    }
}
void Gfx::applyCRT(){
    if(!crtEnabled())return;
    const auto view=viewport();std::string error;
    if(!crt_.draw(drawableW_,drawableH_,view.width,view.height,error)){
        crtFailure_=error;
        std::fprintf(stderr,"CRT disabled for this run: %s\n",error.c_str());std::string ignored;crt_.enable(false,ignored);
    }
}
void Gfx::present() {
    applyBrightness();
    applyCRT();
    SDL_GL_SwapWindow(window_);
}

} // namespace vyruz
