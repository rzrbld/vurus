#pragma once
#include <SDL.h>
#include <optional>
#include <string>

namespace vyruz {
// Linux presentation adapter. No game/world coordinates or timing live here.
struct WindowVideoState {
    Uint32 flags=0;SDL_DisplayMode display{};SDL_Rect bounds{};
    std::optional<SDL_Rect> windowed;
};
inline bool fullscreenWindow(Uint32 flags){return (flags&SDL_WINDOW_FULLSCREEN)!=0;}
inline WindowVideoState captureWindowVideo(SDL_Window* window,std::optional<SDL_Rect> windowed){
    WindowVideoState state;state.flags=SDL_GetWindowFlags(window);state.windowed=windowed;
    SDL_GetWindowDisplayMode(window,&state.display);SDL_GetWindowPosition(window,&state.bounds.x,&state.bounds.y);SDL_GetWindowSize(window,&state.bounds.w,&state.bounds.h);return state;
}
inline bool restoreWindowVideo(SDL_Window* window,const WindowVideoState& state,std::optional<SDL_Rect>& windowed){
    bool ok=SDL_SetWindowFullscreen(window,0)==0;
    ok=(SDL_SetWindowDisplayMode(window,&state.display)==0)&&ok;
    const auto bounds=fullscreenWindow(state.flags)&&state.windowed?*state.windowed:state.bounds;
    SDL_SetWindowSize(window,bounds.w,bounds.h);SDL_SetWindowPosition(window,bounds.x,bounds.y);
    SDL_SetWindowBordered(window,(state.flags&SDL_WINDOW_BORDERLESS)?SDL_FALSE:SDL_TRUE);
    if(fullscreenWindow(state.flags))ok=(SDL_SetWindowFullscreen(window,state.flags&SDL_WINDOW_FULLSCREEN_DESKTOP)==0)&&ok;
    windowed=state.windowed;return ok;
}
// mode:0windowed,1exclusive fullscreen,2desktop fullscreen,3borderless window.
inline bool applyWindowVideo(SDL_Window* window,int mode,int width,int height,bool resize,
        std::optional<SDL_Rect>& windowed,std::string& error){
    if(!window||mode<0||mode>3){error="Invalid display mode";return false;}
    const auto before=captureWindowVideo(window,windowed);
    const bool wasFull=fullscreenWindow(before.flags),full=mode==1||mode==2;
    const Uint32 desired=mode==1?Uint32(SDL_WINDOW_FULLSCREEN):mode==2?Uint32(SDL_WINDOW_FULLSCREEN_DESKTOP):0;
    const auto fail=[&](const std::string& reason){
        const bool restored=restoreWindowVideo(window,before,windowed);
        error=reason+(restored?"":" (display rollback failed)");return false;
    };
    SDL_DisplayMode chosen{};
    if(mode==1){
        const int display=SDL_GetWindowDisplayIndex(window);SDL_DisplayMode wanted{};wanted.w=width;wanted.h=height;
        if(display<0||!SDL_GetClosestDisplayMode(display,&wanted,&chosen)||chosen.w!=width||chosen.h!=height){
            error="Resolution unavailable in Fullscreen; choose a supported resolution first";return false;
        }
    }
    if(!wasFull&&full)windowed=before.bounds;
    const bool change=(before.flags&SDL_WINDOW_FULLSCREEN_DESKTOP)!=desired;
    if(wasFull&&change&&SDL_SetWindowFullscreen(window,0)!=0)return fail(SDL_GetError());
    if(mode==1&&(change||resize)&&SDL_SetWindowDisplayMode(window,&chosen)!=0)return fail(SDL_GetError());
    if(!full){
        SDL_SetWindowBordered(window,mode==3?SDL_FALSE:SDL_TRUE);
        if(wasFull&&windowed){const auto bounds=*windowed;SDL_SetWindowPosition(window,bounds.x,bounds.y);if(!resize)SDL_SetWindowSize(window,bounds.w,bounds.h);}
        if(resize)SDL_SetWindowSize(window,width,height);
    }else if((change||resize)&&SDL_SetWindowFullscreen(window,desired)!=0)return fail(SDL_GetError());
    int actualWidth=0,actualHeight=0;SDL_GetWindowSize(window,&actualWidth,&actualHeight);
    if(resize&&mode!=2&&(actualWidth!=width||actualHeight!=height))return fail("Display size unavailable");
    const auto flags=SDL_GetWindowFlags(window);
    if((flags&SDL_WINDOW_FULLSCREEN_DESKTOP)!=desired || (!full&&bool(flags&SDL_WINDOW_BORDERLESS)!=(mode==3)))return fail("Display mode unavailable");
    if(!full){SDL_Rect bounds{};SDL_GetWindowPosition(window,&bounds.x,&bounds.y);SDL_GetWindowSize(window,&bounds.w,&bounds.h);windowed=bounds;}
    error.clear();return true;
}
}
