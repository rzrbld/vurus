#include "video/window_mode.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <iostream>
int main(){
 SDL_SetMainReady();
 if(SDL_Init(SDL_INIT_VIDEO)!=0){std::cerr<<"SDL video: "<<SDL_GetError()<<"\n";return 1;}auto* w=SDL_CreateWindow("video modes",40,50,640,480,SDL_WINDOW_SHOWN|SDL_WINDOW_RESIZABLE);assert(w);
 std::optional<SDL_Rect> windowed;std::string error;
 assert(vyruz::applyWindowVideo(w,0,640,480,true,windowed,error));assert(windowed&&windowed->w==640&&windowed->h==480);
 if(std::string(SDL_GetCurrentVideoDriver())=="dummy"){
  assert(!vyruz::applyWindowVideo(w,3,640,480,false,windowed,error)&&!error.empty());
  auto unchanged=vyruz::captureWindowVideo(w,windowed);assert(!(unchanged.flags&SDL_WINDOW_BORDERLESS)&&unchanged.bounds.w==640&&unchanged.bounds.h==480);
  SDL_DestroyWindow(w);SDL_Quit();std::cout<<"PASS unsupported headless backend refuses mode and preserves window\n";return 0;
 }
 assert(vyruz::applyWindowVideo(w,3,640,480,false,windowed,error));assert(SDL_GetWindowFlags(w)&SDL_WINDOW_BORDERLESS);
 SDL_DisplayMode desktop{};assert(SDL_GetDesktopDisplayMode(SDL_GetWindowDisplayIndex(w),&desktop)==0);
 assert(vyruz::applyWindowVideo(w,2,640,480,false,windowed,error));auto full=vyruz::captureWindowVideo(w,windowed);
 assert((full.flags&SDL_WINDOW_FULLSCREEN_DESKTOP)==SDL_WINDOW_FULLSCREEN_DESKTOP);assert(full.bounds.w==desktop.w&&full.bounds.h==desktop.h);
 assert(vyruz::applyWindowVideo(w,0,640,480,false,windowed,error));auto ordinary=vyruz::captureWindowVideo(w,windowed);assert(!vyruz::fullscreenWindow(ordinary.flags)&&!(ordinary.flags&SDL_WINDOW_BORDERLESS));assert(ordinary.bounds.w==640&&ordinary.bounds.h==480);
 assert(vyruz::applyWindowVideo(w,1,desktop.w,desktop.h,true,windowed,error));assert((SDL_GetWindowFlags(w)&SDL_WINDOW_FULLSCREEN_DESKTOP)==SDL_WINDOW_FULLSCREEN);
 auto exclusive=vyruz::captureWindowVideo(w,windowed);
 assert(!vyruz::applyWindowVideo(w,1,16383,16381,true,windowed,error)&&!error.empty());auto unchanged=vyruz::captureWindowVideo(w,windowed);assert(unchanged.flags==exclusive.flags&&unchanged.bounds.w==exclusive.bounds.w&&unchanged.bounds.h==exclusive.bounds.h);
 assert(vyruz::applyWindowVideo(w,0,640,480,false,windowed,error));ordinary=vyruz::captureWindowVideo(w,windowed);assert(ordinary.bounds.w==640&&ordinary.bounds.h==480);
 assert(vyruz::restoreWindowVideo(w,full,windowed));assert((SDL_GetWindowFlags(w)&SDL_WINDOW_FULLSCREEN_DESKTOP)==SDL_WINDOW_FULLSCREEN_DESKTOP);
 assert(vyruz::applyWindowVideo(w,3,800,600,true,windowed,error));ordinary=vyruz::captureWindowVideo(w,windowed);assert(ordinary.bounds.w==800&&ordinary.bounds.h==600&&(ordinary.flags&SDL_WINDOW_BORDERLESS));
 assert(!vyruz::applyWindowVideo(w,4,640,480,true,windowed,error));assert(vyruz::restoreWindowVideo(w,exclusive,windowed));assert((SDL_GetWindowFlags(w)&SDL_WINDOW_FULLSCREEN_DESKTOP)==SDL_WINDOW_FULLSCREEN);
 SDL_DestroyWindow(w);SDL_Quit();std::cout<<"PASS SDL four modes, desktop geometry, remembered window bounds, exclusive support rejection and transactional restoration\n";
}
