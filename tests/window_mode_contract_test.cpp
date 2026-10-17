// SDL call-boundary fixture: exercises transitions/faults independently of a WM.
#include <SDL.h>
#include <optional>
#include <string>
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <iostream>
namespace fixture {
Uint32 flags=SDL_WINDOW_RESIZABLE;SDL_Rect bounds{40,50,640,480};SDL_DisplayMode mode{0,1920,1080,60,nullptr};
bool failFull=false,failDisplay=false,ignoreSize=false,ignoreBorder=false;
Uint32 getFlags(SDL_Window*){return flags;}
int getDisplay(SDL_Window*,SDL_DisplayMode* out){*out=mode;return 0;}
void getPosition(SDL_Window*,int* x,int* y){*x=bounds.x;*y=bounds.y;}
void getSize(SDL_Window*,int* w,int* h){*w=bounds.w;*h=bounds.h;}
int setFull(SDL_Window*,Uint32 value){if(failFull){failFull=false;return -1;}flags=(flags&~SDL_WINDOW_FULLSCREEN_DESKTOP)|value;if(value){bounds.w=(value==SDL_WINDOW_FULLSCREEN_DESKTOP?1920:mode.w);bounds.h=(value==SDL_WINDOW_FULLSCREEN_DESKTOP?1080:mode.h);}return 0;}
int setDisplay(SDL_Window*,const SDL_DisplayMode* value){if(failDisplay){failDisplay=false;return -1;}mode=*value;return 0;}
void setPosition(SDL_Window*,int x,int y){bounds.x=x;bounds.y=y;}
void setSize(SDL_Window*,int w,int h){if(ignoreSize){ignoreSize=false;return;}bounds.w=w;bounds.h=h;}
void setBorder(SDL_Window*,SDL_bool value){if(ignoreBorder){ignoreBorder=false;return;}if(value)flags&=~SDL_WINDOW_BORDERLESS;else flags|=SDL_WINDOW_BORDERLESS;}
int getIndex(SDL_Window*){return 0;}
SDL_DisplayMode* closest(int,const SDL_DisplayMode* wanted,SDL_DisplayMode* result){if((wanted->w!=640||wanted->h!=480)&&(wanted->w!=1920||wanted->h!=1080))return nullptr;*result=*wanted;result->refresh_rate=60;return result;}
const char* error(){return "injected SDL failure";}
}
#define SDL_GetWindowFlags fixture::getFlags
#define SDL_GetWindowDisplayMode fixture::getDisplay
#define SDL_GetWindowPosition fixture::getPosition
#define SDL_GetWindowSize fixture::getSize
#define SDL_SetWindowFullscreen fixture::setFull
#define SDL_SetWindowDisplayMode fixture::setDisplay
#define SDL_SetWindowPosition fixture::setPosition
#define SDL_SetWindowSize fixture::setSize
#define SDL_SetWindowBordered fixture::setBorder
#define SDL_GetWindowDisplayIndex fixture::getIndex
#define SDL_GetClosestDisplayMode fixture::closest
#define SDL_GetError fixture::error
#include "video/window_mode.h"
int main(){
 using namespace vyruz;auto* window=reinterpret_cast<SDL_Window*>(1);std::optional<SDL_Rect> remembered;std::string error;
 const auto apply=[&](int m,int w=640,int h=480,bool resize=false){return applyWindowVideo(window,m,w,h,resize,remembered,error);};
 assert(apply(0));const auto initial=captureWindowVideo(window,remembered);
 for(int from=0;from<4;++from)for(int to=0;to<4;++to){assert(restoreWindowVideo(window,initial,remembered));assert(apply(from));assert(apply(to));assert((fixture::flags&SDL_WINDOW_FULLSCREEN_DESKTOP)==(to==1?Uint32(SDL_WINDOW_FULLSCREEN):to==2?Uint32(SDL_WINDOW_FULLSCREEN_DESKTOP):0));if(to==2)assert(fixture::bounds.w==1920&&fixture::bounds.h==1080);else assert(fixture::bounds.w==640&&fixture::bounds.h==480);}
 assert(restoreWindowVideo(window,initial,remembered));fixture::failFull=true;assert(!apply(2));assert(fixture::flags==initial.flags&&fixture::bounds.w==640);
 fixture::failDisplay=true;assert(!apply(1));assert(fixture::flags==initial.flags);
 fixture::ignoreBorder=true;assert(!apply(3));assert(fixture::flags==initial.flags);
 fixture::ignoreSize=true;assert(!apply(0,800,600,true));assert(fixture::bounds.w==640&&fixture::bounds.h==480);
 assert(apply(2));auto desktop=captureWindowVideo(window,remembered);assert(!apply(1,16383,16381,true));assert(fixture::flags==desktop.flags&&fixture::bounds.w==1920);
 assert(apply(3,800,600,true));assert(fixture::bounds.w==800&&fixture::bounds.h==600);assert(restoreWindowVideo(window,desktop,remembered));assert(apply(0));assert(fixture::bounds.w==640&&fixture::bounds.h==480);
 std::cout<<"PASS 16 transitions, remembered bounds, exact exclusive rejection, SDL fault/ignored-call rollback (software API fixture)\n";
}
