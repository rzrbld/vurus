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

#if VYRUZ_GLES
    std::string error;
    if(!gles::initialize(error)){std::fprintf(stderr,"GLES renderer: %s\n",error.c_str());shutdown();return false;}
#endif
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
    std::vector<uint8_t>().swap(bitmapScratch_);
    crt_.shutdown();sceneEffects_.shutdown();smaa_.shutdown();
    for(const auto& [id,sprite]:spriteTextures_)if(sprite.texture)glDeleteTextures(1,&sprite.texture);
    spriteTextures_.clear();
    deleteTexture(distortion_);distortion_=0;distortionW_=distortionH_=0;
    deleteTexture(brightness_);brightness_=0;brightnessW_=brightnessH_=0;
#if VYRUZ_GLES
    if(ctx_){
        if(previewFramebuffer_)glDeleteFramebuffers(1,&previewFramebuffer_);
        if(previewTexture_)glDeleteTextures(1,&previewTexture_);
        previewFramebuffer_=previewTexture_=0;previewValid_=false;
        gles::shutdown();
    }
#endif
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
    if(sprite)spriteTextures_[tex]={w,h,0,0,false,size_t(w)*h*4};
    return tex;
}

GLuint Gfx::uploadTexture(const GafBitmap& bitmap,bool sprite) {
    if(!bitmap.valid())return 0;
    constexpr size_t retainedLimit=1024*1024;
    std::vector<uint8_t> temporary;
    auto& rgba=bitmap.rgb555.size()<=retainedLimit/4?bitmapScratch_:temporary;
    // reserve exact high-water sizes rather than allowing vector growth to
    // retain more than the stated scratch budget.
    const auto bytes=bitmap.rgb555.size()*4;
    if(&rgba==&bitmapScratch_ && rgba.capacity()<bytes){std::vector<uint8_t> next(bytes);rgba.swap(next);}
    bitmap.expand(rgba);
    return uploadTexture(rgba.data(),bitmap.width,bitmap.height,sprite);
}

size_t Gfx::spriteTextureBytes(GLuint tex) const {
    const auto found=spriteTextures_.find(tex);
    return found==spriteTextures_.end()?0:found->second.bytes;
}

void Gfx::deleteTexture(GLuint tex) {
    if(auto found=spriteTextures_.find(tex);found!=spriteTextures_.end()) {
        if(found->second.texture){glDeleteTextures(1,&found->second.texture);if(current_==found->second.texture)current_=0;}
        spriteTextures_.erase(found);
    }
    if (tex) glDeleteTextures(1, &tex);
    if (current_ == tex) current_ = 0;
}

GLuint Gfx::filteredTexture(GLuint original,const GafBitmap* source) {
    const auto found=spriteTextures_.find(original);
    if(found==spriteTextures_.end())return original;
    auto& sprite=found->second;
    const bool needMips=spriteFiltering_==2;
    if(!sprite.texture || (needMips && !sprite.mipmaps)) {
        SpriteMip base{sprite.width,sprite.height,{}};
        if(source && source->valid() && source->width==sprite.width && source->height==sprite.height){
            // Production scene draws supply their live compact bitmap. No
            // GPU synchronization/readback and no second RGBA base copy.
            source->expand(base.rgba);
        }else{
            // Compatibility path for raw-RGBA texture clients without CPU art.
            glBindTexture(GL_TEXTURE_2D,original);
            base.rgba.resize(size_t(sprite.width)*sprite.height*4);
            glPixelStorei(GL_PACK_ALIGNMENT,1);
#if VYRUZ_GLES
            gles::readTexture(sprite.width,sprite.height,base.rgba.data());
#else
            glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_UNSIGNED_BYTE,base.rgba.data());
#endif
        }
        const bool fresh=!sprite.texture;
        if(fresh)glGenTextures(1,&sprite.texture);
        glBindTexture(GL_TEXTURE_2D,sprite.texture);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        glPixelStorei(GL_UNPACK_ALIGNMENT,1);
        visitFilteredSpriteMips(std::move(base),needMips,[&](size_t level,const SpriteMip& mip){
            // Bilinear -> Trilinear upgrades the existing filtered texture;
            // its identical level0 need not be uploaded again.
            if(level || fresh) {
                glTexImage2D(GL_TEXTURE_2D,GLint(level),GL_RGBA,mip.width,mip.height,0,GL_RGBA,GL_UNSIGNED_BYTE,mip.rgba.data());
                sprite.bytes+=size_t(mip.width)*mip.height*4;
            }
        });
        sprite.mipmaps=needMips;
        current_=sprite.texture;
    }
    if(sprite.mode!=spriteFiltering_) {
        glBindTexture(GL_TEXTURE_2D,sprite.texture);current_=sprite.texture;
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,spriteFiltering_==2?GL_LINEAR_MIPMAP_LINEAR:GL_LINEAR);
        sprite.mode=spriteFiltering_;
    }
    return sprite.texture;
}
void Gfx::beginQuads(GLuint tex,bool filterSprite,const GafBitmap* filterSource) {
    if(filterSprite && spriteFiltering_!=0)tex=filteredTexture(tex,filterSource);
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
#if VYRUZ_GLES
void Gfx::capturePreview(){
    if(drawableW_<=0||drawableH_<=0)return;
    GLint oldRead=0,oldDraw=0,bound=0;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&oldRead);glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&oldDraw);
    glGetIntegerv(GL_TEXTURE_BINDING_2D,&bound);
    if(!previewTexture_){
        glGenTextures(1,&previewTexture_);glBindTexture(GL_TEXTURE_2D,previewTexture_);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,224,168,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
        glGenFramebuffers(1,&previewFramebuffer_);glBindFramebuffer(GL_DRAW_FRAMEBUFFER,previewFramebuffer_);
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,previewTexture_,0);
        glBindTexture(GL_TEXTURE_2D,bound);
    }
    glBindFramebuffer(GL_READ_FRAMEBUFFER,0);glBindFramebuffer(GL_DRAW_FRAMEBUFFER,previewFramebuffer_);
    const auto view=viewport();
    const int cw=std::clamp(int(drawableW_*std::min(640.f,view.width)/view.width),1,drawableW_);
    const int ch=std::clamp(int(drawableH_*std::min(480.f,view.height)/view.height),1,drawableH_);
    glBlitFramebuffer(0,drawableH_-ch,cw,drawableH_,0,0,224,168,GL_COLOR_BUFFER_BIT,GL_NEAREST);
    previewValid_=glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE;
    glBindFramebuffer(GL_READ_FRAMEBUFFER,oldRead);glBindFramebuffer(GL_DRAW_FRAMEBUFFER,oldDraw);
}
std::vector<uint8_t> Gfx::presentedPreview() const{
    if(!previewValid_)return {};
    GLint oldRead=0,alignment=0;glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&oldRead);glGetIntegerv(GL_PACK_ALIGNMENT,&alignment);
    glBindFramebuffer(GL_READ_FRAMEBUFFER,previewFramebuffer_);glPixelStorei(GL_PACK_ALIGNMENT,1);
    std::vector<uint8_t> result(224*168*4);glReadPixels(0,0,224,168,GL_RGBA,GL_UNSIGNED_BYTE,result.data());
    glBindFramebuffer(GL_READ_FRAMEBUFFER,oldRead);glPixelStorei(GL_PACK_ALIGNMENT,alignment);
    return result;
}
#endif
void Gfx::present() {
    applyBrightness();
    applyCRT();
#if VYRUZ_GLES
    capturePreview();
#endif
    if(swapEnabled_)SDL_GL_SwapWindow(window_);
}

} // namespace vyruz
