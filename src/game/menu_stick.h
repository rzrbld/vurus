#pragma once
#include <SDL.h>
#include <cstdlib>
namespace vyruz {
// Menu-only axis hysteresis. One press per deflection, release near neutral;
// preserves the existing D-pad/keyboard edge-only row navigation policy.
struct MenuStick {
    int x=0,y=0;
    void reset(){x=y=0;}
    bool translate(SDL_Event& e){
        if(e.type!=SDL_CONTROLLERAXISMOTION)return false;
        if(e.caxis.axis!=SDL_CONTROLLER_AXIS_LEFTX&&e.caxis.axis!=SDL_CONTROLLER_AXIS_LEFTY)return false;
        const bool horizontal=e.caxis.axis==SDL_CONTROLLER_AXIS_LEFTX;
        int& previous=horizontal?x:y;
        const int value=e.caxis.value;
        const int next=value>18000?1:value< -18000?-1:std::abs(value)<10000?0:previous;
        if(next==previous)return false;
        const auto id=e.caxis.which;const int direction=next?next:previous;previous=next;
        e={};e.type=next?SDL_CONTROLLERBUTTONDOWN:SDL_CONTROLLERBUTTONUP;
        e.cbutton.which=id;e.cbutton.button=horizontal?(direction<0?SDL_CONTROLLER_BUTTON_DPAD_LEFT:SDL_CONTROLLER_BUTTON_DPAD_RIGHT):(direction<0?SDL_CONTROLLER_BUTTON_DPAD_UP:SDL_CONTROLLER_BUTTON_DPAD_DOWN);
        return true;
    }
};
}
