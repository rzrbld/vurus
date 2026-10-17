#include "game/controller.h"
#include <algorithm>
#include <cmath>
#include <numbers>
#include <utility>
namespace vyruz {
void ControllerInput::close(){if(pad_)SDL_GameControllerClose(pad_);pad_=nullptr;haveAim_=false;}
void ControllerInput::configure(int moveSensitivity,int aimSensitivity,int deadzone){
    if(aimSensitivity_!=std::clamp(aimSensitivity,10,100) || deadzone_!=std::clamp(deadzone,0,95))haveAim_=false;
    moveSensitivity_=std::clamp(moveSensitivity,10,100);
    aimSensitivity_=std::clamp(aimSensitivity,10,100);
    deadzone_=std::clamp(deadzone,0,95);
}
void ControllerInput::scan(){
    if(pad_&&!SDL_GameControllerGetAttached(pad_))close();
    if(pad_)return;
    for(int i=0;i<SDL_NumJoysticks();++i)
        if(SDL_IsGameController(i) && (pad_=SDL_GameControllerOpen(i)))break;
}
bool ControllerInput::owns(SDL_JoystickID id) const {
    return pad_ && SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pad_))==id;
}
bool ControllerInput::event(const SDL_Event& e){
    const auto identity=[&]{return pad_?SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pad_)):SDL_JoystickID(-1);};
    const auto before=identity();
    const bool remapped=e.type==SDL_CONTROLLERDEVICEREMAPPED&&owns(e.cdevice.which);
    if(e.type==SDL_CONTROLLERDEVICEREMOVED && owns(e.cdevice.which))close();
    if(e.type==SDL_CONTROLLERDEVICEADDED||e.type==SDL_CONTROLLERDEVICEREMOVED||remapped)scan();
    const bool changed=before!=identity()||remapped;
    if(changed)resetAim();
    return changed;
}
SDL_FPoint ControllerInput::stick(Sint16 x,Sint16 y,float deadzone){
    const float dx=std::max(-1.f,x/32767.f),dy=std::max(-1.f,y/32767.f);
    const float length=std::hypot(dx,dy);
    deadzone=std::clamp(deadzone,0.f,.95f);
    if(length<=deadzone)return {};
    const float scale=(std::min(length,1.f)-deadzone)/(1-deadzone)/length;
    return {dx*scale,dy*scale};
}
MovementCommand ControllerInput::movement(float seconds,int facingDirection,bool resetAim) {
    if(!connected())return {};
    auto axis=[&](SDL_GameControllerAxis a){return SDL_GameControllerGetAxis(pad_,a);};
    auto left=stick(axis(SDL_CONTROLLER_AXIS_LEFTX),axis(SDL_CONTROLLER_AXIS_LEFTY),deadzone_/100.f);
    auto right=stick(axis(SDL_CONTROLLER_AXIS_RIGHTX),axis(SDL_CONTROLLER_AXIS_RIGHTY),deadzone_/100.f);
    const float strength=std::hypot(left.x,left.y);
    if(moveSensitivity_<100 && strength>0){
        const float scale=std::pow(std::min(strength,1.f),100.f/moveSensitivity_)/strength;
        left.x*=scale;left.y*=scale;
    }
    const float aimStrength=std::hypot(right.x,right.y);
    if(resetAim)haveAim_=false;
    if(aimStrength>0){
        const float target=std::atan2(right.y,right.x);
        if(!haveAim_ && facingDirection>=0 && aimSensitivity_<100){
            aimAngle_=3*std::numbers::pi_v<float>/4-((facingDirection%24+24)%24)*std::numbers::pi_v<float>/12;
            haveAim_=true;
        }
        if(aimSensitivity_==100 || !haveAim_)aimAngle_=target;
        else {
            const float dt=std::isfinite(seconds)?std::clamp(seconds,0.f,.1f):0.f;
            constexpr float pi=std::numbers::pi_v<float>;
            const float delta=std::remainder(target-aimAngle_,2*pi);
            const float limit=aimSensitivity_*9.f*pi/180.f*dt;
            aimAngle_=std::remainder(aimAngle_+std::clamp(delta,-limit,limit),2*pi);
        }
        haveAim_=true;
        // Preserve the accepted exact stick values at default sensitivity.
        if(aimSensitivity_<100){right.x=std::cos(aimAngle_)*aimStrength;right.y=std::sin(aimAngle_)*aimStrength;}
    }else haveAim_=false;
    return {-left.y,left.x,right.x!=0||right.y!=0,right.x,right.y};
}
bool ControllerInput::button(int value) const {
    return connected() && value>=0 && value<SDL_CONTROLLER_BUTTON_MAX && SDL_GameControllerGetButton(pad_,static_cast<SDL_GameControllerButton>(value));
}
bool ControllerInput::firing(int fireButton) const {
    return connected() && (SDL_GameControllerGetAxis(pad_,SDL_CONTROLLER_AXIS_TRIGGERRIGHT)>8192 || button(fireButton));
}
bool ControllerInput::confirmationHeld() const {
    if(!connected())return false;
    for(int i=0;i<SDL_CONTROLLER_BUTTON_MAX;++i)if(button(i))return true;
    return SDL_GameControllerGetAxis(pad_,SDL_CONTROLLER_AXIS_TRIGGERRIGHT)>8192;
}
bool ControllerInput::controlsNeutral() const {
    if(!connected())return true;
    if(confirmationHeld()||SDL_GameControllerGetAxis(pad_,SDL_CONTROLLER_AXIS_TRIGGERLEFT)>8192)return false;
    for(const auto pair:{std::pair{SDL_CONTROLLER_AXIS_LEFTX,SDL_CONTROLLER_AXIS_LEFTY},
                         std::pair{SDL_CONTROLLER_AXIS_RIGHTX,SDL_CONTROLLER_AXIS_RIGHTY}}){
        const auto value=stick(SDL_GameControllerGetAxis(pad_,pair.first),SDL_GameControllerGetAxis(pad_,pair.second),deadzone_/100.f);
        if(value.x!=0||value.y!=0)return false;
    }
    return true;
}
}
