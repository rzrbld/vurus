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
 glDisable(GL_TEXTURE_2D);gfx.clear(.25f,.25f,.25f);gfx.setGamma(100);gfx.applyBrightness();need(pixel(10,10)[0]>64&&pixel(10,10)[0]<128,"bounded brightness");gfx.setGamma(50);
 gfx.clear(1,0,0);gfx.present();
#if VYRUZ_GLES
 const auto preview=gfx.presentedPreview();need(preview.size()==224*168*4,"presented thumbnail size");need(preview[0]==255&&preview[1]==0,"presented thumbnail pixels");
 gfx.beginFrame(640,480);gfx.clear(0,1,0);const auto previous=gfx.presentedPreview();need(previous==preview,"preview survives backbuffer changes");
#endif
 need(glGetError()==GL_NO_ERROR,"GL error after production rendering operations");
 gfx.deleteTexture(texture);gfx.shutdown();SDL_DestroyWindow(window);SDL_Quit();std::puts("renderer transforms/clip/alpha/specular/filter/brightness/preview passed");return 0;
}catch(const std::exception&e){std::fprintf(stderr,"render backend: %s\n",e.what());return 1;}}
