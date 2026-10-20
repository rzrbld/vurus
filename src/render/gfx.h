#pragma once
#include "render/smaa.h"
#include "assets/gaf_bitmap.h"
#include <SDL.h>
#include "render/gl_api.h"

#include <cstdint>
#include <map>
#include <vector>
#include "render/viewport.h"
#include "render/crt.h"
#include "render/crt_geometry.h"
#include "render/hud_layout.h"

namespace vyruz {

// Minimal OpenGL renderer for the native port. Uses a fixed-function,
// compatibility-profile context (GL 1.1 immediate mode) so it works on
// Mesa/NVIDIA/SteamOS without an extension loader. Screen space is
// top-left origin, y-down, orthogonal projection to a virtual resolution.
class Gfx {
public:
    bool init(SDL_Window* window);
    void shutdown();

    bool vsyncEnabled() const { return vsync_; }
    bool setVsync(bool enabled,std::string& error);
    int windowWidth() const { return winW_; }
    int windowHeight() const { return winH_; }
    Viewport viewport() const {return Viewport::fit(winW_,winH_);}
    HudLayout hudLayout() const {return HudLayout::fit(viewport(),hudAspect_);}
    void setHudAspect(int mode) {hudAspect_=mode;}
    int drawableWidth() const {return drawableW_;}
    int drawableHeight() const {return drawableH_;}
    void resize(int w, int h) { winW_ = w; winH_ = h; }

    void beginFrame(int vw, int vh);
    void clear(float r, float g, float b);

    GLuint uploadTexture(const uint8_t* rgba, int w, int h,bool sprite=false);
    GLuint uploadTexture(const GafBitmap&,bool sprite=false);
    // GLES atlas cache for compact animation frames and font glyphs. Desktop
    // retains standalone textures. Filterable frames borrow their source again
    // at beginQuads when filtering is enabled.
    GLuint uploadSpriteTexture(const GafBitmap&,bool filterable=true);
    size_t atlasTextureBytes() const;
    size_t spriteAtlasSlackBytes() const;
    void deleteTexture(GLuint tex);
    // Uploaded RGBA payload for a registered sprite and its retained filter copy.
    // Excludes driver overhead; returns zero for unregistered/deleted textures.
    size_t spriteTextureBytes(GLuint tex) const;

    // Optional compact source must match the uploaded pixels. It is borrowed
    // only for this call, never retained.
    void beginQuads(GLuint tex,bool filterSprite=false,const GafBitmap* filterSource=nullptr);
    // Prepare visible frames before their pages enter the draw stream. This
    // avoids repeated texture mutation while earlier quads sample the same page.
    void prepareSpriteTexture(GLuint tex,bool filterSprite,const GafBitmap* source);
    void setSpriteFiltering(int mode) { spriteFiltering_=mode; }
    int spriteFiltering() const { return spriteFiltering_; }
    // Vertices in virtual screen space (top-left origin).
    void quad(float x0, float y0, float u0, float v0,
              float x1, float y1, float u1, float v1,
              float x2, float y2, float u2, float v2,
              float x3, float y3, float u3, float v3);
    void endQuads();

    void distortScanlines(float phase);
    // Bounded midtone adjustment, keeping black/white and useful contrast.
    // Separate from present so framebuffer audits can inspect the same pass.
    void applyBrightness();
    bool setCRT(bool enabled,std::string& error){const bool ok=crt_.enable(enabled,error);crtFailure_=ok?std::string{}:error;return ok;}
    void setRetroCRT(bool warm,bool bright=false){crt_.setWarm(warm,bright);}
    const std::string& crtError()const{return crtFailure_;}
    bool crtEnabled()const{return crt_.enabled();}
    CrtPoint mousePoint(float x,float y)const;
    void applyCRT();
    bool setSceneEffects(bool fxaa,bool bloom,bool sharpen,std::string& error,bool smaa=false){if(fxaa&&smaa){error="Choose FXAA or SMAA";return false;}if(!smaa_.enable(smaa,error))return false;return sceneEffects_.configureScene(fxaa,bloom,sharpen,error);}
    bool smaaEnabled()const{return smaa_.enabled();}
    bool sceneEffectsEnabled()const{return sceneEffects_.enabled()||smaa_.enabled();}
    void applySceneEffects();
    void present();
    // Diagnostic readback must occur before swapping a GLES backbuffer.
    void setSwapEnabled(bool enabled) { swapEnabled_=enabled; }
#if VYRUZ_GLES
    // GLES cannot read the front buffer after swap. Capture only requested saves.
    void requestPreview() {previewRequested_=true;previewValid_=false;}
    void cancelPreview() {previewRequested_=previewValid_=false;}
    std::vector<uint8_t> presentedPreview() const;
#endif
    void setGamma(int value) { gamma_=value; }

private:
    std::vector<uint8_t> bitmapScratch_; // capped at1MiB; large uploads use temporary storage
    bool swapEnabled_=true;
#if VYRUZ_GLES
    int atlasMaximum_=0;
    struct AtlasRect {int x=0,y=0,w=0,h=0;};
    struct AtlasRegion {GLuint page=0;AtlasRect rect;};
    struct AtlasPage {int size=0,kind=0;size_t users=0;std::vector<AtlasRect> free;};
    struct AtlasSprite {AtlasRegion plain,linear;bool linearAttempted=false;};
    size_t atlasWorldBytes_=0,atlasWorldUsed_=0,atlasFontBytes_=0;
    std::map<GLuint,AtlasPage> atlasPages_;
    std::map<GLuint,AtlasSprite> atlasSprites_;
    AtlasRegion atlasUpload(const uint8_t*,int,int,int kind);
    void atlasRelease(AtlasRegion);
    // Mapping resets at every beginQuads, including ordinary terrain/UI draws.
    float atlasU_=0,atlasV_=0,atlasW_=1,atlasH_=1;
    GLuint previewTexture_=0,previewFramebuffer_=0;
    bool previewValid_=false,previewRequested_=false;
    void capturePreview();
#endif
    SmaaPass smaa_;
    CrtPass crt_,sceneEffects_{true};
    std::string crtFailure_;
    struct FilteredTexture {int width,height;GLuint texture=0;int mode=0;bool mipmaps=false;size_t bytes=0;};
    std::map<GLuint,FilteredTexture> spriteTextures_;
    GLuint filteredTexture(GLuint original,const GafBitmap* source);
    GLuint resolveQuadTexture(GLuint,bool,const GafBitmap*);
    int spriteFiltering_=0;
    SDL_Window* window_ = nullptr;
    SDL_GLContext ctx_ = nullptr;
    int winW_ = 0, winH_ = 0;
    int vw_ = 640, vh_ = 480;
    int drawableW_=0,drawableH_=0;
    GLuint current_ = 0, distortion_=0;
    int distortionW_=0,distortionH_=0;
    GLuint brightness_=0;
    int brightnessW_=0,brightnessH_=0;
    bool vsync_ = false;
    int gamma_=50;
    int hudAspect_=0;
};

} // namespace vyruz
