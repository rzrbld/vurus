#include "render/first_run_screen.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
using namespace vyruz;
static FirstRunAction pump(FirstRunController& input){
    FirstRunAction action=FirstRunAction::None;SDL_Event e;
    SDL_PumpEvents();while(SDL_PollEvent(&e)){auto a=input.event(e);if(a!=FirstRunAction::None)action=a;}
    return action;
}
static SDL_Joystick* attach(int& index){
    index=SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_GAMECONTROLLER,SDL_CONTROLLER_AXIS_MAX,SDL_CONTROLLER_BUTTON_MAX,0);assert(index>=0);
    auto* pad=SDL_JoystickOpen(index);assert(pad);
    SDL_JoystickSetVirtualAxis(pad,SDL_CONTROLLER_AXIS_TRIGGERLEFT,-32768);
    SDL_JoystickSetVirtualAxis(pad,SDL_CONTROLLER_AXIS_TRIGGERRIGHT,-32768);SDL_JoystickUpdate();return pad;
}
static FirstRunAction button(FirstRunController& input,SDL_Joystick* pad,int button,bool down){
    assert(SDL_JoystickSetVirtualButton(pad,button,down)==0);SDL_JoystickUpdate();return pump(input);
}
static void detach(SDL_Joystick* pad){
    const auto id=SDL_JoystickInstanceID(pad);int index=0;while(index<SDL_NumJoysticks()&&SDL_JoystickGetDeviceInstanceID(index)!=id)++index;
    assert(index<SDL_NumJoysticks());SDL_JoystickClose(pad);assert(SDL_JoystickDetachVirtual(index)==0);
}
int main(){
 SDL_SetMainReady();
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS,"1");assert(SDL_Init(SDL_INIT_GAMECONTROLLER)==0);
    FirstRunController input;int first=0;auto* pad=attach(first);assert(pump(input)==FirstRunAction::None);
    assert(button(input,pad,SDL_CONTROLLER_BUTTON_B,true)==FirstRunAction::Cancel);
    assert(button(input,pad,SDL_CONTROLLER_BUTTON_B,false)==FirstRunAction::None);
    assert(button(input,pad,SDL_CONTROLLER_BUTTON_A,true)==FirstRunAction::Retry);
    assert(button(input,pad,SDL_CONTROLLER_BUTTON_A,false)==FirstRunAction::None);
    SDL_Event focus{};focus.type=SDL_WINDOWEVENT;focus.window.event=SDL_WINDOWEVENT_FOCUS_LOST;input.event(focus);
    assert(button(input,pad,SDL_CONTROLLER_BUTTON_B,true)==FirstRunAction::None);
    focus.window.event=SDL_WINDOWEVENT_FOCUS_GAINED;input.event(focus);
    SDL_Event held{};held.type=SDL_CONTROLLERBUTTONDOWN;held.cbutton.which=SDL_JoystickInstanceID(pad);held.cbutton.button=SDL_CONTROLLER_BUTTON_B;
    assert(input.event(held)==FirstRunAction::None);
    button(input,pad,SDL_CONTROLLER_BUTTON_B,false);assert(button(input,pad,SDL_CONTROLLER_BUTTON_B,true)==FirstRunAction::Cancel);
    button(input,pad,SDL_CONTROLLER_BUTTON_B,false);
    // A second pad must not control the active device's modal actions.
    int second=0;auto* other=attach(second);pump(input);assert(button(input,other,SDL_CONTROLLER_BUTTON_A,true)==FirstRunAction::None);
    detach(pad);pump(input);held.cbutton.which=SDL_JoystickInstanceID(other);held.cbutton.button=SDL_CONTROLLER_BUTTON_A;
    assert(input.event(held)==FirstRunAction::None); // fallback held A cannot retry
    button(input,other,SDL_CONTROLLER_BUTTON_A,false);assert(button(input,other,SDL_CONTROLLER_BUTTON_A,true)==FirstRunAction::Retry);
    button(input,other,SDL_CONTROLLER_BUTTON_A,false);detach(other);pump(input);input.close();SDL_Quit();
}
