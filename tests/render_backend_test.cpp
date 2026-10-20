#include "render/gfx.h"
#include "render/texture_residency.h"
#include <array>
#include <stdexcept>
#include <cstdio>
#include <cmath>
using namespace vyruz;
static void need(bool value,const char* message){if(!value)throw std::runtime_error(message);}
static std::array<unsigned char,4> pixel(int x,int y,int height=480){std::array<unsigned char,4> p{};glReadPixels(x,height-1-y,1,1,GL_RGBA,GL_UNSIGNED_BYTE,p.data());return p;}
static void rectangle(float x,float y,float w,float h){glBegin(GL_QUADS);glVertex2f(x,y);glVertex2f(x+w,y);glVertex2f(x+w,y+h);glVertex2f(x,y+h);glEnd();}
int main(){try{
 need(Viewport::pixelAligned(12.49f,640,640)==12 && Viewport::pixelAligned(12.51f,640,640)==13,"camera physical pixel alignment");
 need(Viewport::pixelAligned(-12.51f,640,640)==-13 && Viewport::pixelAligned(12.26f,640,1280)==12.5f,"camera negative/high-DPI alignment");
 need(SDL_Init(SDL_INIT_VIDEO)==0,SDL_GetError());
#if VYRUZ_GLES
 SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_ES);SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,3);SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,0);
#else
 SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,2);SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,1);
#endif
 auto* window=SDL_CreateWindow("Renderer test",0,0,640,480,SDL_WINDOW_OPENGL|SDL_WINDOW_HIDDEN);need(window,SDL_GetError());Gfx gfx;need(gfx.init(window),"renderer init");
 std::string swapError;
 need(gfx.setVsync(false,swapError)&&!gfx.vsyncEnabled(),"VSync disabled at runtime");
 const bool syncAvailable=gfx.setVsync(true,swapError);
 if(!syncAvailable)need(!swapError.empty(),"unsupported swap control reports an error");
 need(gfx.setVsync(false,swapError)&&!gfx.vsyncEnabled(),"VSync can be disabled again");
 GLint maxTexture=0;glGetIntegerv(GL_MAX_TEXTURE_SIZE,&maxTexture);std::printf("maximum texture size: %d\n",maxTexture);
 gfx.beginFrame(640,480);gfx.clear(0,0,0);glDisable(GL_TEXTURE_2D);glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
 glColor4f(1,0,0,.5f);rectangle(0,0,40,40);glColor4f(0,1,0,.5f);rectangle(10,10,40,40);glColor4f(0,0,1,.5f);rectangle(20,20,40,40);
 auto blended=pixel(25,25);need(std::abs(int(blended[0])-32)<=1&&std::abs(int(blended[1])-64)<=1&&std::abs(int(blended[2])-128)<=1,"ordered overlapping per-vertex tint");
 const uint8_t redTex[]={255,0,0,255},greenTex[]={0,255,0,255},blueTex[]={0,0,255,255};
 auto ta=gfx.uploadTexture(redTex,1,1),tb=gfx.uploadTexture(greenTex,1,1);
 gfx.clear(0,0,0);glEnable(GL_TEXTURE_2D);glDisable(GL_BLEND);glColor4f(1,1,1,1);
 auto box=[&](GLuint tex,float x){gfx.beginQuads(tex);gfx.quad(x,x,0,0,x+40,x,1,0,x+40,x+40,1,1,x,x+40,0,1);gfx.endQuads();};
 box(ta,0);box(tb,10);box(ta,20);need(pixel(15,15)[1]==255&&pixel(25,25)[0]==255,"A B A texture order is preserved");
 gfx.clear(0,0,0);box(ta,0);glBindTexture(GL_TEXTURE_2D,ta);glTexSubImage2D(GL_TEXTURE_2D,0,0,0,1,1,GL_RGBA,GL_UNSIGNED_BYTE,blueTex);box(ta,50);
 need(pixel(5,5)[0]==255&&pixel(55,55)[2]==255,"queued draws complete before texture mutation");
 gfx.clear(0,0,0);box(ta,0);gfx.deleteTexture(ta);need(pixel(5,5)[2]==255,"queued draws complete before texture deletion");gfx.deleteTexture(tb);
 glDisable(GL_TEXTURE_2D);glColor4f(1,0,0,1);rectangle(0,0,40,40);gfx.clear(0,1,0);need(pixel(5,5)[1]==255&&pixel(5,5)[0]==0,"clear is an ordering barrier");
 // Queue different matrices, alpha tests and blend factors before any readback.
 gfx.clear(0,0,0);glDisable(GL_BLEND);glDisable(GL_ALPHA_TEST);
 glColor4f(1,0,0,1);rectangle(0,0,20,20);
 glPushMatrix();glTranslatef(30,0,0);glColor4f(0,1,0,1);rectangle(0,0,20,20);glPopMatrix();
 glEnable(GL_ALPHA_TEST);glAlphaFunc(GL_GREATER,.75f);glColor4f(0,0,1,.5f);rectangle(0,0,20,20);glDisable(GL_ALPHA_TEST);
 glEnable(GL_BLEND);glBlendFunc(GL_ONE,GL_ONE);glColor4f(0,0,1,1);rectangle(30,0,20,20);
 need(pixel(5,5)[0]==255&&pixel(5,5)[2]==0&&pixel(35,5)[1]==255&&pixel(35,5)[2]==255,"deferred matrix alpha and blend state");
 gfx.clear(0,0,0);glDisable(GL_BLEND);glColor4f(1,0,0,1);rectangle(0,0,40,40);
 glScissor(20,440,20,40);glEnable(GL_SCISSOR_TEST);glColor4f(0,1,0,1);rectangle(0,0,40,40);glDisable(GL_SCISSOR_TEST);
 need(pixel(5,5)[0]==255&&pixel(25,5)[1]==255,"scissor is an ordering barrier");
 gfx.clear(0,0,0);glColor4f(1,0,0,1);
 glBegin(GL_TRIANGLES);glVertex2f(100,0);glVertex2f(100,20);glEnd();
 glBegin(GL_TRIANGLES);glVertex2f(120,0);glEnd();
 need(pixel(105,5)[0]==0,"incomplete primitives do not join across ends");
 glBegin(GL_LINES);glVertex2f(100.5f,30.5f);glEnd();
 glBegin(GL_LINES);glVertex2f(120.5f,30.5f);glEnd();
 need(pixel(110,29)[0]==0&&pixel(110,30)[0]==0,"incomplete lines do not join across ends");
#if VYRUZ_GLES
 // Exceed the queue limit through both many small ends and one oversized end.
 gfx.clear(0,0,0);glColor4f(1,0,0,1);
 for(int i=0;i<11000;++i)rectangle(0,0,1,1);
 glColor4f(0,0,1,1);glBegin(GL_QUADS);
 for(int i=0;i<11000;++i){glVertex2f(2,0);glVertex2f(3,0);glVertex2f(3,1);glVertex2f(2,1);}glEnd();
 need(pixel(0,0)[0]==255&&pixel(2,0)[2]==255,"bounded queue and oversized draw preserve order");
#endif
 gfx.beginFrame(640,480);gfx.clear(0,0,0);glDisable(GL_TEXTURE_2D);glDisable(GL_BLEND);
 glPushMatrix();glTranslatef(40,50,0);glScalef(2,2,1);glColor4f(1,0,0,1);rectangle(0,0,20,20);glPopMatrix();
 need(pixel(45,55)[0]==255&&pixel(20,20)[0]==0,"matrix transform");
 const GLdouble top[]={0,1,0,-80},bottom[]={0,-1,0,100};
 glPushMatrix();glTranslatef(100,20,0);glClipPlane(GL_CLIP_PLANE0,top);glClipPlane(GL_CLIP_PLANE1,bottom);glEnable(GL_CLIP_PLANE0);glEnable(GL_CLIP_PLANE1);
 glColor4f(0,1,0,1);rectangle(0,60,60,60);glDisable(GL_CLIP_PLANE0);glDisable(GL_CLIP_PLANE1);glPopMatrix();
 need(pixel(110,110)[1]==255&&pixel(110,90)[1]==0&&pixel(110,130)[1]==0,"transformed clip planes");
 const uint8_t rgba[]={64,96,128,100,64,96,128,255,64,96,128,100,64,96,128,255};
 auto texture=gfx.uploadTexture(rgba,2,2,true);glEnable(GL_TEXTURE_2D);glColor4f(1,1,1,1);glEnable(GL_ALPHA_TEST);glAlphaFunc(GL_GREATER,.5f);
 gfx.beginQuads(texture);gfx.quad(200,100,0,0,240,100,1,0,240,140,1,1,200,140,0,1);gfx.endQuads();
 need(pixel(205,110)[2]==0&&pixel(235,110)[2]==128,"alpha test");glDisable(GL_ALPHA_TEST);
 const float white[]={.25f,.25f,.25f,0};glTexEnvfv(GL_TEXTURE_ENV,GL_TEXTURE_ENV_COLOR,white);glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_COMBINE);glTexEnvi(GL_TEXTURE_ENV,GL_COMBINE_RGB,GL_ADD);
 glTexEnvi(GL_TEXTURE_ENV,GL_SOURCE0_RGB,GL_TEXTURE);glTexEnvi(GL_TEXTURE_ENV,GL_SOURCE1_RGB,GL_CONSTANT);glTexEnvi(GL_TEXTURE_ENV,GL_OPERAND0_RGB,GL_SRC_COLOR);glTexEnvi(GL_TEXTURE_ENV,GL_OPERAND1_RGB,GL_SRC_COLOR);glTexEnvi(GL_TEXTURE_ENV,GL_COMBINE_ALPHA,GL_REPLACE);glTexEnvi(GL_TEXTURE_ENV,GL_SOURCE0_ALPHA,GL_TEXTURE);
 gfx.beginQuads(texture);gfx.quad(250,100,0,0,290,100,1,0,290,140,1,1,250,140,0,1);gfx.endQuads();need(std::abs(int(pixel(280,110)[0])-128)<=1,"specular add");
 glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_MODULATE);
 gfx.setSpriteFiltering(2);gfx.beginQuads(texture,true);gfx.quad(300,100,0,0,340,100,1,0,340,140,1,1,300,140,0,1);gfx.endQuads();need(pixel(335,110)[2]==128,"filtered texture readback");
 // Compact uploads must match the prior RGBA route, including key RGB,
 // bit15, filtering and recreation after texture eviction, on GL and GLES.
 GafBitmap packed;packed.width=3;packed.height=2;packed.rgb555={0,0x7c1f,0xfc1f,0x7c00,0x03e0,0x001f};
 const auto expanded=packed.rgbaImage();
 auto capture=[&](GLuint tex,int mode,const GafBitmap* source=nullptr){gfx.beginFrame(640,480);gfx.clear(.2f,.3f,.4f);glEnable(GL_TEXTURE_2D);glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);glColor4f(1,1,1,1);gfx.setSpriteFiltering(mode);
  gfx.beginQuads(tex,true,source);gfx.quad(20,20,0,0,63,20,1,0,63,57,1,1,20,57,0,1);gfx.endQuads();
  std::vector<uint8_t> pixels(640*480*4);glReadPixels(0,0,640,480,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());return pixels;};
 const auto reference=gfx.uploadTexture(expanded.rgba.data(),3,2,true);
 for(int mode=0;mode<3;++mode){const auto expected=capture(reference,mode);
  for(int repeat=0;repeat<2;++repeat){const auto compact=gfx.uploadTexture(packed,true);need(compact!=0,"compact upload");need(capture(compact,mode,&packed)==expected,"compact filtering/reupload pixels");gfx.deleteTexture(compact);}}
 const auto retained=gfx.uploadTexture(packed,true);
 need(gfx.spriteTextureBytes(retained)==24,"original payload only");
 bool upgraded=false;
 GLint filteredId=0;
 for(int mode:{1,0,2,1,0,2}){
  const auto expected=capture(reference,mode);need(capture(retained,mode,&packed)==expected,"persistent filter mode switching");
#if !VYRUZ_GLES
  if(mode){GLint id=0;glGetIntegerv(GL_TEXTURE_BINDING_2D,&id);
   if(!filteredId){filteredId=id;GLint width=0;glGetTexLevelParameteriv(GL_TEXTURE_2D,1,GL_TEXTURE_WIDTH,&width);need(width==0,"Bilinear must not allocate lower mip levels");}
   else need(id==filteredId,"filtered texture preserved across upgrade");
   if(mode==2){GLint width=0;glGetTexLevelParameteriv(GL_TEXTURE_2D,1,GL_TEXTURE_WIDTH,&width);need(width==1,"Trilinear creates lower levels");}}
#endif
  upgraded|=mode==2;
  need(gfx.spriteTextureBytes(retained)==(upgraded?52u:48u),"retained allocation across filter toggles");
  need(glIsTexture(retained),"original texture retained");
 }
 gfx.deleteTexture(retained);
 need(gfx.spriteTextureBytes(retained)==0,"deleted payload removed");
 gfx.deleteTexture(reference);
 // Thin, odd-sized mip chain:7x1 +3x1 +1x1, plus original7x1.
 GafBitmap thin;thin.width=7;thin.height=1;thin.rgb555.assign(7,0x03e0);
 auto thinTexture=gfx.uploadTexture(thin,true);
 TextureResidency residency;residency.beginDraw();
 residency.touch(thinTexture,gfx.spriteTextureBytes(thinTexture));
 need(residency.bytes()==28,"initial cache payload");
 capture(thinTexture,2,&thin);
 residency.touch(thinTexture,gfx.spriteTextureBytes(thinTexture));
 need(residency.bytes()==72,"cache grows after mip upload");
 residency.trim(0,0,[&](auto id){gfx.deleteTexture(id);});
 need(thinTexture!=0,"visible texture pinned");
 residency.beginDraw();const auto evicted=thinTexture;
 residency.trim(0,0,[&](auto id){gfx.deleteTexture(id);});
 need(thinTexture==0 && residency.bytes()==0 && gfx.spriteTextureBytes(evicted)==0,"eviction releases payload");
 thinTexture=gfx.uploadTexture(thin,true);
 need(gfx.spriteTextureBytes(thinTexture)==28,"recreation starts without filtered copy");
 gfx.deleteTexture(thinTexture);
 GafBitmap large;large.width=600;large.height=600;large.rgb555.assign(600*600,0x03e0);
 const auto largeTexture=gfx.uploadTexture(large);need(largeTexture!=0,"large temporary expansion");gfx.deleteTexture(largeTexture);
 GafBitmap empty;need(gfx.uploadTexture(empty)==0,"empty compact bitmap rejected");
 // Dynamic page slack is re-evaluated after each cache eviction.
 TextureResidency pageCache;unsigned pageSlots[]={101,102,103};size_t slack=0,removed=0;
 pageCache.beginDraw();for(auto& id:pageSlots)pageCache.touch(id,10);
 pageCache.beginDraw();pageCache.trimWithOverhead(25,10,[&]{return slack;},[&](unsigned){++removed;slack=removed==3?0:removed*10;});
 need(removed==3 && pageCache.bytes()==0,"eviction accounts for growing atlas slack");
 // Atlas identities share physical pages without sharing frame lifetime, UVs,
 // filtering or draw order. Compare against standalone pixels, including odd
 // dimensions, flips, clipped glyphs and minification/magnification.
 GafBitmap atlasArt;atlasArt.width=7;atlasArt.height=5;
 atlasArt.rgb555={0,0,0x7c00,0x7c00,0x7c00,0,0,
  0,0x03e0,0x03e0,0x001f,0x001f,0x03e0,0,
  0x7c00,0x03e0,0x001f,0x7fff,0x001f,0x03e0,0x7c00,
  0,0x03e0,0x03e0,0x001f,0x001f,0x03e0,0,
  0,0,0x7c00,0x7c00,0x7c00,0,0};
 auto atlasControl=gfx.uploadTexture(atlasArt,true);
 auto atlasFrame=gfx.uploadSpriteTexture(atlasArt,true);
 auto atlasNeighbor=gfx.uploadSpriteTexture(thin,true);
 auto atlasCapture=[&](GLuint id,int mode){
  gfx.beginFrame(640,480);gfx.clear(.2f,.3f,.4f);glEnable(GL_TEXTURE_2D);glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);glColor4f(.8f,.6f,1,.75f);gfx.setSpriteFiltering(mode);
  for(float size:{3.f,7.f,31.f}){
   gfx.beginQuads(id,true,&atlasArt);
   gfx.quad(size*5,20,1,0,size*5+size,20,0,0,size*5+size,20+size,0,1,size*5,20+size,1,1);gfx.endQuads();
   gfx.beginQuads(id,true,&atlasArt);
   gfx.quad(size*5,70,0,0,size*5+size*.5f,70,.5f,0,size*5+size*.5f,70+size,.5f,1,size*5,70+size,0,1);gfx.endQuads();
  }
  std::vector<uint8_t> out(640*480*4);glReadPixels(0,0,640,480,GL_RGBA,GL_UNSIGNED_BYTE,out.data());return out;
 };
 for(int mode:{0,1,2,0,2,1}){
  const auto expected=atlasCapture(atlasControl,mode),actual=atlasCapture(atlasFrame,mode);
  for(size_t i=0;i<actual.size();++i)need(std::abs(int(actual[i])-expected[i])<=1,"atlas filtering/flip/clip pixels");
 }
 gfx.deleteTexture(atlasNeighbor);
 need(atlasCapture(atlasFrame,0)==atlasCapture(atlasControl,0),"neighbor eviction preserves live frame");
 gfx.deleteTexture(atlasFrame);gfx.deleteTexture(atlasControl);
 need(gfx.atlasTextureBytes()==0,"empty atlas pages released");
 auto glyphA=gfx.uploadSpriteTexture(atlasArt,false),glyphB=gfx.uploadSpriteTexture(thin,false);
 gfx.beginQuads(glyphA);gfx.endQuads();GLint pageA=0;glGetIntegerv(GL_TEXTURE_BINDING_2D,&pageA);
 gfx.beginQuads(glyphB);gfx.endQuads();GLint pageB=0;glGetIntegerv(GL_TEXTURE_BINDING_2D,&pageB);
#if VYRUZ_GLES
 need(pageA==pageB,"font glyphs share a page");
#endif
 gfx.deleteTexture(glyphA);gfx.deleteTexture(glyphB);
 GafBitmap solid;solid.width=solid.height=2;solid.rgb555.assign(4,0x7c00);
 auto atlasRed=gfx.uploadSpriteTexture(solid,false);solid.rgb555.assign(4,0x001f);
 auto atlasBlue=gfx.uploadSpriteTexture(solid,false);
 gfx.beginFrame(640,480);gfx.clear(0,0,0);glEnable(GL_TEXTURE_2D);glDisable(GL_BLEND);glColor4f(1,1,1,1);
 box(atlasRed,0);box(atlasBlue,10);box(atlasRed,20);
 need(pixel(15,15)[2]==255 && pixel(25,25)[0]==255,"atlas A B A overlap order");
 gfx.clear(0,0,0);box(atlasRed,0);gfx.deleteTexture(atlasRed);
 solid.rgb555.assign(4,0x03e0);auto atlasGreen=gfx.uploadSpriteTexture(solid,false);box(atlasGreen,50);
 need(pixel(5,5)[0]==255 && pixel(55,55)[1]==255,"queued atlas region safe before reuse");
 gfx.deleteTexture(atlasBlue);gfx.deleteTexture(atlasGreen);
 GafBitmap crowded;crowded.width=250;crowded.height=250;crowded.rgb555.assign(250*250,0x03e0);
 std::vector<GLuint> cache;
 for(int i=0;i<150;++i)cache.push_back(gfx.uploadSpriteTexture(crowded,true));
 need(gfx.atlasTextureBytes()<=32u*1024*1024,"bounded world atlas pages with standalone overflow");
 for(auto id:cache)gfx.deleteTexture(id);
 need(gfx.atlasTextureBytes()==0 && gfx.spriteAtlasSlackBytes()==0,"atlas churn releases all storage");
 glDisable(GL_TEXTURE_2D);gfx.clear(.25f,.25f,.25f);gfx.setGamma(100);gfx.applyBrightness();need(pixel(10,10)[0]>64&&pixel(10,10)[0]<128,"bounded brightness");gfx.setGamma(50);
 gfx.clear(1,0,0);gfx.present();
#if VYRUZ_GLES
 need(gfx.presentedPreview().empty(),"ordinary frames do not capture thumbnails");
 gfx.requestPreview();gfx.clear(1,0,0);gfx.present();
 const auto preview=gfx.presentedPreview();need(preview.size()==224*168*4,"presented thumbnail size");need(preview[0]==255&&preview[1]==0,"presented thumbnail pixels");
 gfx.beginFrame(640,480);gfx.clear(0,1,0);gfx.present();const auto previous=gfx.presentedPreview();need(previous==preview,"ordinary frames do not refresh thumbnails");
 gfx.requestPreview();need(gfx.presentedPreview().empty(),"new request invalidates stale thumbnail");
 gfx.clear(0,1,0);gfx.present();const auto next=gfx.presentedPreview();need(next.size()==preview.size()&&next[0]==0&&next[1]==255,"requested frame replaces thumbnail");
 gfx.requestPreview();gfx.cancelPreview();gfx.clear(0,0,1);gfx.present();need(gfx.presentedPreview().empty(),"cancelled request does not capture a frame");
#endif
 need(glGetError()==GL_NO_ERROR,"GL error after production rendering operations");
 gfx.deleteTexture(texture);gfx.shutdown();SDL_DestroyWindow(window);SDL_Quit();std::puts("renderer transforms/clip/alpha/specular/filter/brightness/preview passed");return 0;
}catch(const std::exception&e){std::fprintf(stderr,"render backend: %s\n",e.what());return 1;}}
