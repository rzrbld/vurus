#pragma once
#include <SDL.h>
#include "game/controls.h"
namespace vyruz {
// SDL's controller mapping database provides hardware remapping, including
// SDL_GAMECONTROLLERCONFIG overrides. Gameplay never depends on raw buttons.
class ControllerInput {
public:
    ~ControllerInput(){close();}
    ControllerInput()=default;
    ControllerInput(const ControllerInput&)=delete;
    ControllerInput& operator=(const ControllerInput&)=delete;
    void scan();
    void close();
    // True when active device identity/mapping changes (added/removed/remapped).
    // Lifecycle events are processed even while modal UI is open.
    bool event(const SDL_Event& event);
    bool owns(SDL_JoystickID id) const;
    void configure(int moveSensitivity,int aimSensitivity,int deadzone);
    MovementCommand movement(float seconds=1.f/60,int facingDirection=-1,bool resetAim=false);
    bool firing(int button=SDL_CONTROLLER_BUTTON_A) const;
    bool button(int value) const;
    bool connected() const {return pad_&&SDL_GameControllerGetAttached(pad_);}
    bool controlsNeutral() const;
    bool confirmationHeld() const;
    void resetAim(){haveAim_=false;}
    static SDL_FPoint stick(Sint16 x,Sint16 y,float deadzone=.22f);
private:
    SDL_GameController* pad_=nullptr;
    int moveSensitivity_=100,aimSensitivity_=100,deadzone_=22;
    bool haveAim_=false;
    float aimAngle_=0;
};
}
