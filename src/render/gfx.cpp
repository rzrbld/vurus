#include "render/gfx.h"
#include "render/sprite_filter.h"

#include <cstdio>
#include <cstring>
#include <algorithm>
#include <cmath>
#include <stdexcept>

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
    std::string swapError;
    if(!setVsync(true,swapError)){SDL_GL_SetSwapInterval(0);vsync_=false;}
    std::printf("frame pacing: %s\n",vsync_?"VSync":"software deadline pacing (VSync unavailable)");
    SDL_GetWindowSize(window_, &winW_, &winH_);

#if VYRUZ_GLES
    std::string error;
    if(!gles::initialize(error)){std::fprintf(stderr,"GLES renderer: %s\n",error.c_str());shutdown();return false;}
    glGetIntegerv(GL_MAX_TEXTURE_SIZE,&atlasMaximum_);
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

bool Gfx::setVsync(bool enabled,std::string& error) {
    const char* driver=SDL_GetCurrentVideoDriver();
    // Offscreen Mesa accepts interval1 without a display retrace. Keep the
    // software deadline clock for offscreen/dummy contexts, as at startup.
    const bool headless=driver && (!std::strcmp(driver,"offscreen") || !std::strcmp(driver,"dummy"));
    const int interval=enabled && !headless?1:0;
    if(SDL_GL_GetSwapInterval()!=interval && SDL_GL_SetSwapInterval(interval)!=0) {
        error=std::string("VSync: ")+SDL_GetError();
        vsync_=!headless && SDL_GL_GetSwapInterval()!=0;
        return false;
    }
    vsync_=!headless && SDL_GL_GetSwapInterval()!=0;
    error.clear();return true;
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
        for(const auto& [id,sprite]:atlasSprites_)glDeleteTextures(1,&id);
        atlasSprites_.clear();
        for(const auto& [id,page]:atlasPages_)glDeleteTextures(1,&id);
        atlasPages_.clear();atlasWorldBytes_=atlasWorldUsed_=atlasFontBytes_=0;
        if(previewFramebuffer_)glDeleteFramebuffers(1,&previewFramebuffer_);
        if(previewTexture_)glDeleteTextures(1,&previewTexture_);
        previewFramebuffer_=previewTexture_=0;previewValid_=previewRequested_=false;
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

#if VYRUZ_GLES
Gfx::AtlasRegion Gfx::atlasUpload(const uint8_t* rgba,int w,int h,int kind,GLuint unpackBuffer,size_t unpackOffset) {
    // Separate nearest world, bilinear world, and nearest font pages. Bounded
    // independently of the animation cache; large/overflow frames stay standalone.
    const int size=std::min(kind==2?256:(w+2<=512 && h+2<=512)?512:1024,atlasMaximum_);
    const int rw=w+2,rh=h+2;
    if(w<=0 || h<=0 || rw>size || rh>size)return {};
    auto chosen=atlasPages_.end();size_t slot=0;int waste=0x7fffffff;
    for(auto it=atlasPages_.begin();it!=atlasPages_.end();++it) {
        auto& page=it->second;
        if(page.kind!=kind)continue;
        for(size_t i=0;i<page.free.size();++i) {
            const auto r=page.free[i];
            if(r.w>=rw && r.h>=rh && r.w*r.h-rw*rh<waste) {
                chosen=it;slot=i;waste=r.w*r.h-rw*rh;
            }
        }
    }
    if(chosen==atlasPages_.end()) {
        const size_t bytes=size_t(size)*size*4;
        if((kind==2?atlasFontBytes_+bytes>2u*1024*1024:atlasWorldBytes_+bytes>32u*1024*1024))return {};
        GLuint id=0;glGenTextures(1,&id);glBindTexture(GL_TEXTURE_2D,id);current_=id;
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,kind==1?GL_LINEAR:GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,kind==1?GL_LINEAR:GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,size,size,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
        chosen=atlasPages_.emplace(id,AtlasPage{size,kind,0,{{0,0,size,size}}}).first;slot=0;
        (kind==2?atlasFontBytes_:atlasWorldBytes_)+=bytes;
    }
    auto& page=chosen->second;const auto space=page.free[slot];
    page.free.erase(page.free.begin()+slot);
    if(space.w>rw)page.free.push_back({space.x+rw,space.y,space.w-rw,rh});
    if(space.h>rh)page.free.push_back({space.x,space.y+rh,space.w,space.h-rh});
    ++page.users;
    if(kind!=2)atlasWorldUsed_+=size_t(rw)*rh*4;
    // Replicated one-texel border is clamp-to-edge for both nearest and linear.
    // Transparent filtered RGB has already been dilated by the existing helper.
    std::vector<uint8_t> padded;
    if(!unpackBuffer){
        padded.resize(size_t(rw)*rh*4);
        for(int y=0;y<rh;++y)for(int x=0;x<rw;++x)
            std::memcpy(&padded[(size_t(y)*rw+x)*4],rgba+(size_t(std::clamp(y-1,0,h-1))*w+std::clamp(x-1,0,w-1))*4,4);
    }
    glBindTexture(GL_TEXTURE_2D,chosen->first);current_=chosen->first;
    glPixelStorei(GL_UNPACK_ALIGNMENT,1);
    GLint previous=0;if(unpackBuffer){glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING,&previous);glBindBuffer(GL_PIXEL_UNPACK_BUFFER,unpackBuffer);}
    glTexSubImage2D(GL_TEXTURE_2D,0,space.x,space.y,rw,rh,GL_RGBA,GL_UNSIGNED_BYTE,unpackBuffer?reinterpret_cast<const void*>(unpackOffset):padded.data());
    if(unpackBuffer)glBindBuffer(GL_PIXEL_UNPACK_BUFFER,static_cast<GLuint>(previous));
    return {chosen->first,{space.x,space.y,rw,rh}};
}
void Gfx::atlasRelease(AtlasRegion region) {
    if(!region.page)return;
    auto it=atlasPages_.find(region.page);if(it==atlasPages_.end())return;
    auto& page=it->second;
    if(page.kind!=2)atlasWorldUsed_-=size_t(region.rect.w)*region.rect.h*4;
    if(!--page.users) {
        glDeleteTextures(1,&region.page);if(current_==region.page)current_=0;
        (page.kind==2?atlasFontBytes_:atlasWorldBytes_)-=size_t(page.size)*page.size*4;
        atlasPages_.erase(it);return;
    }
    // Reuse holes after frame eviction. Coalesce guillotine siblings so repeated
    // animation/level changes cannot accumulate unusable small allocations.
    auto r=region.rect;
    for(size_t i=0;i<page.free.size();) {
        const auto b=page.free[i];AtlasRect merged;
        if(r.y==b.y && r.h==b.h && (r.x+r.w==b.x || b.x+b.w==r.x))merged={std::min(r.x,b.x),r.y,r.w+b.w,r.h};
        else if(r.x==b.x && r.w==b.w && (r.y+r.h==b.y || b.y+b.h==r.y))merged={r.x,std::min(r.y,b.y),r.w,r.h+b.h};
        else {++i;continue;}
        r=merged;page.free.erase(page.free.begin()+i);i=0;
    }
    page.free.push_back(r);
}
#endif
GLuint Gfx::uploadSpriteTexture(const GafBitmap& bitmap,bool filterable) {
#if VYRUZ_GLES
    if(bitmap.valid()) {
        const auto rgba=bitmap.rgbaImage();
        const auto region=atlasUpload(rgba.rgba.data(),bitmap.width,bitmap.height,filterable?0:2);
        if(region.page) {
            // Logical frame identities retain the existing residency slots. Only
            // atlas pages own pixel storage; this name is never bound for drawing.
            GLuint id=0;glGenTextures(1,&id);
            atlasSprites_[id]={region,{}};
            if(filterable)spriteTextures_[id]={bitmap.width,bitmap.height,0,0,false,size_t(region.rect.w)*region.rect.h*4};
            return id;
        }
    }
#endif
    return uploadTexture(bitmap,filterable);
}
GLuint Gfx::uploadFontTexture(const GafBitmap& bitmap,GLuint unpackBuffer,size_t unpackOffset) {
#if VYRUZ_GLES
    if(unpackBuffer&&bitmap.valid()){
        const auto region=atlasUpload(nullptr,bitmap.width,bitmap.height,2,unpackBuffer,unpackOffset);
        if(region.page){GLuint id=0;glGenTextures(1,&id);atlasSprites_[id]={region,{}};return id;}
    }
#else
    (void)unpackBuffer;(void)unpackOffset;
#endif
    return uploadSpriteTexture(bitmap,false);
}
size_t Gfx::atlasTextureBytes() const {
#if VYRUZ_GLES
    return atlasWorldBytes_+atlasFontBytes_;
#else
    return 0;
#endif
}

size_t Gfx::spriteAtlasSlackBytes() const {
#if VYRUZ_GLES
    return atlasWorldBytes_-atlasWorldUsed_;
#else
    return 0;
#endif
}

size_t Gfx::spriteTextureBytes(GLuint tex) const {
    const auto found=spriteTextures_.find(tex);
    return found==spriteTextures_.end()?0:found->second.bytes;
}

void Gfx::deleteTexture(GLuint tex) {
#if VYRUZ_GLES
    if(auto it=atlasSprites_.find(tex);it!=atlasSprites_.end()) {
        // A freed region may be overwritten by the next upload on this page.
        gles::flush();
        atlasRelease(it->second.plain);atlasRelease(it->second.linear);
        atlasSprites_.erase(it);
    }
#endif
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
GLuint Gfx::resolveQuadTexture(GLuint tex,bool filterSprite,const GafBitmap* filterSource) {
#if VYRUZ_GLES
    atlasU_=atlasV_=0;atlasW_=atlasH_=1;
    if(auto it=atlasSprites_.find(tex);it!=atlasSprites_.end()) {
        auto region=it->second.plain;
        if(filterSprite && spriteFiltering_!=0) {
            if(!filterSource || !filterSource->valid() || filterSource->width!=region.rect.w-2 || filterSource->height!=region.rect.h-2)
                throw std::logic_error("filtered atlas draw requires matching compact source");
            if(spriteFiltering_==1 && !it->second.linearAttempted) {
                it->second.linearAttempted=true;
                auto base=filterSource->rgbaImage();
                visitFilteredSpriteMips(SpriteMip{base.width,base.height,std::move(base.rgba)},false,[&](size_t,const SpriteMip& mip){
                    it->second.linear=atlasUpload(mip.rgba.data(),mip.width,mip.height,1);
                });
                if(it->second.linear.page)spriteTextures_.at(tex).bytes+=size_t(it->second.linear.rect.w)*it->second.linear.rect.h*4;
            }
            // Independent mip chains preserve the accepted trilinear filter.
            // World atlas callers always supply their compact source here.
            if(spriteFiltering_==1 && it->second.linear.page)region=it->second.linear;
            else {
                region={};
                tex=filteredTexture(tex,filterSource);
            }
        }
        if(region.page) {
            const float size=float(atlasPages_.at(region.page).size);
            atlasU_=(region.rect.x+1)/size;atlasV_=(region.rect.y+1)/size;
            atlasW_=(region.rect.w-2)/size;atlasH_=(region.rect.h-2)/size;
            tex=region.page;
        }
    }else
#endif
    if(filterSprite && spriteFiltering_!=0)tex=filteredTexture(tex,filterSource);
    return tex;
}
void Gfx::prepareSpriteTexture(GLuint tex,bool filterSprite,const GafBitmap* source) {
#if VYRUZ_GLES
    resolveQuadTexture(tex,filterSprite,source);
#else
    (void)tex;(void)filterSprite;(void)source;
#endif
}
void Gfx::beginQuads(GLuint tex,bool filterSprite,const GafBitmap* filterSource) {
    tex=resolveQuadTexture(tex,filterSprite,filterSource);
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
#if VYRUZ_GLES
    u0=atlasU_+u0*atlasW_;v0=atlasV_+v0*atlasH_;
    u1=atlasU_+u1*atlasW_;v1=atlasV_+v1*atlasH_;
    u2=atlasU_+u2*atlasW_;v2=atlasV_+v2*atlasH_;
    u3=atlasU_+u3*atlasW_;v3=atlasV_+v3*atlasH_;
#endif
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
#if VYRUZ_GLES
    gles::flush();
#endif
    applyBrightness();
    applyCRT();
#if VYRUZ_GLES
    if(previewRequested_){previewRequested_=false;capturePreview();}
#endif
#if VYRUZ_GLES
    gles::flush();
#endif
    if(swapEnabled_)SDL_GL_SwapWindow(window_);
}

} // namespace vyruz
