#include "game/controller.h"
#include "game/menu_stick.h"
#include "render/viewport.h"
#include "render/presentation.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cmath>
int main(){
 SDL_SetMainReady();
    using namespace vyruz;
    MenuStick menuStick;
    const auto axis=[&](int value){SDL_Event e{};e.type=SDL_CONTROLLERAXISMOTION;e.caxis.axis=SDL_CONTROLLER_AXIS_LEFTX;e.caxis.value=value;return e;};
    auto event=axis(19000);assert(menuStick.translate(event)&&event.type==SDL_CONTROLLERBUTTONDOWN&&event.cbutton.button==SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
    event=axis(15000);assert(!menuStick.translate(event));
    event=axis(-19000);assert(menuStick.translate(event)&&event.cbutton.button==SDL_CONTROLLER_BUTTON_DPAD_LEFT);
    event=axis(-5000);assert(menuStick.translate(event)&&event.type==SDL_CONTROLLERBUTTONUP);
    event=axis(5000);assert(!menuStick.translate(event));
    auto zero=ControllerInput::stick(3000,-3000);assert(zero.x==0&&zero.y==0);
    auto diag=ControllerInput::stick(32767,32767);assert(std::abs(std::hypot(diag.x,diag.y)-1)<.0001);
    auto negative=ControllerInput::stick(-32768,0);assert(negative.x==-1);
    auto half=ControllerInput::stick(20000,0);assert(half.x>0&&half.x<1);
    PlayerMotion slow,fast;slow.direction=fast.direction=9;
    for(int i=0;i<120;++i){advancePlayerMotion(slow,{.5f,0},1.f/60);advancePlayerMotion(fast,{1,0},1.f/60);}
    // Restored controller preserves reduced analog movement strength.
    assert(std::isfinite(slow.magnitude)&&slow.magnitude>0&&slow.magnitude<fast.magnitude);
    for(auto size:{SDL_Point{1280,800},SDL_Point{1920,1080},SDL_Point{640,480},SDL_Point{480,800}}){
        const auto v=Viewport::fit(size.x,size.y);
        assert(std::abs(v.width/v.height-float(size.x)/size.y)<.001);
        assert(std::abs(v.x(size.x/2.f,size.x)-v.uiX()-320)<.001);
        assert(std::abs(v.y(size.y/2.f,size.y)-v.uiY()-240)<.001);
        assert(v.uiX()>=0&&v.uiY()>=0);
    }
    auto p=presentationPosition({0,0},{3,6},1.f/3);assert(std::abs(p.x-1)<.001&&std::abs(p.y-2)<.001);
    p=presentationPosition({0,0},{100,0},.5);assert(p.x==100);
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS,"1");
    assert(SDL_Init(SDL_INIT_GAMECONTROLLER)==0);
    int index=SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_GAMECONTROLLER,SDL_CONTROLLER_AXIS_MAX,SDL_CONTROLLER_BUTTON_MAX,0);
    assert(index>=0);
    SDL_Joystick* joystick=SDL_JoystickOpen(index);assert(joystick);
    ControllerInput controller;controller.scan();assert(controller.connected());
    assert(SDL_JoystickSetVirtualAxis(joystick,SDL_CONTROLLER_AXIS_RIGHTX,32767)==0);
    assert(SDL_JoystickSetVirtualAxis(joystick,SDL_CONTROLLER_AXIS_LEFTY,-32768)==0);
    assert(SDL_JoystickSetVirtualAxis(joystick,SDL_CONTROLLER_AXIS_TRIGGERRIGHT,32767)==0);
    SDL_JoystickUpdate();auto movement=controller.movement();assert(movement.forward==1&&movement.aimActive&&movement.aimX==1&&controller.firing());
    controller.configure(50,100,22);
    assert(controller.movement().forward==1); // Full throw retains maximum speed.
    SDL_JoystickSetVirtualAxis(joystick,SDL_CONTROLLER_AXIS_LEFTY,-20000);SDL_JoystickUpdate();
    const float reduced=controller.movement().forward;
    controller.configure(100,100,22);const float baseline=controller.movement().forward;
    assert(reduced>0&&reduced<baseline);
    controller.configure(10,100,22);assert(controller.movement().forward<reduced);
    SDL_JoystickSetVirtualAxis(joystick,SDL_CONTROLLER_AXIS_LEFTY,0);SDL_JoystickUpdate();
    controller.configure(100,10,22);
    // Heading 3 is +90 degrees in accepted screen-space aim. First deflection
    // must approach the new target instead of snapping from the player's body.
    auto gentle=controller.movement(1.f/60,3,true);
    assert(gentle.forward==0&&gentle.strafe==0&&gentle.aimY>.99f&&gentle.aimX>0&&gentle.aimX<.03f);
    controller.configure(100,50,22);
    const auto quicker=controller.movement(1.f/60,3,true);
    assert(quicker.aimX>gentle.aimX&&quicker.aimY<gentle.aimY);
    for(int i=0;i<60;++i)movement=controller.movement(1.f/60,3);
    assert(std::abs(movement.aimX-1)<.0001f&&std::abs(movement.aimY)<.0001f);
    ControllerInput sixty,ninety;sixty.scan();ninety.scan();
    sixty.configure(100,10,22);ninety.configure(100,10,22);
    MovementCommand aim60,aim90;
    for(int i=0;i<30;++i)aim60=sixty.movement(1.f/60,3);
    for(int i=0;i<45;++i)aim90=ninety.movement(1.f/90,3);
    assert(std::abs(aim60.aimX-aim90.aimX)<.0001f&&std::abs(aim60.aimY-aim90.aimY)<.0001f);
    assert(std::abs(aim60.aimX-std::sqrt(.5f))<.0001f);
    const auto frozen=sixty.movement(0,3);
    assert(std::abs(frozen.aimX-aim60.aimX)<.0001f);
    SDL_JoystickSetVirtualAxis(joystick,SDL_CONTROLLER_AXIS_RIGHTX,0);SDL_JoystickUpdate();
    assert(!sixty.movement().aimActive);
    SDL_JoystickSetVirtualAxis(joystick,SDL_CONTROLLER_AXIS_RIGHTX,32767);SDL_JoystickUpdate();
    const auto reacquired=sixty.movement(1.f/60,3);
    assert(reacquired.aimX<.03f&&reacquired.aimY>.99f);
    controller.configure(100,100,22);movement=controller.movement(1.f/60,3,true);
    assert(movement.aimX==1&&movement.aimY==0&&movement.forward==0);
    controller.configure(100,100,95);
    SDL_JoystickSetVirtualAxis(joystick,SDL_CONTROLLER_AXIS_RIGHTX,20000);SDL_JoystickUpdate();assert(!controller.movement().aimActive);
    SDL_JoystickSetVirtualAxis(joystick,SDL_CONTROLLER_AXIS_RIGHTX,0);
    SDL_JoystickSetVirtualAxis(joystick,SDL_CONTROLLER_AXIS_TRIGGERLEFT,-32768);
    SDL_JoystickSetVirtualAxis(joystick,SDL_CONTROLLER_AXIS_TRIGGERRIGHT,-32768);SDL_JoystickUpdate();
    assert(controller.controlsNeutral()&&!controller.confirmationHeld());
    SDL_JoystickSetVirtualButton(joystick,SDL_CONTROLLER_BUTTON_B,1);SDL_JoystickUpdate();
    assert(!controller.controlsNeutral()&&controller.confirmationHeld()&&!controller.firing());
    SDL_JoystickSetVirtualButton(joystick,SDL_CONTROLLER_BUTTON_B,0);
    SDL_JoystickSetVirtualAxis(joystick,SDL_CONTROLLER_AXIS_TRIGGERLEFT,32767);SDL_JoystickUpdate();
    assert(!controller.controlsNeutral()&&!controller.confirmationHeld());
    SDL_JoystickSetVirtualAxis(joystick,SDL_CONTROLLER_AXIS_TRIGGERLEFT,-32768);
    SDL_JoystickSetVirtualAxis(joystick,SDL_CONTROLLER_AXIS_LEFTX,32767);SDL_JoystickUpdate();
    assert(!controller.controlsNeutral());
    SDL_JoystickSetVirtualAxis(joystick,SDL_CONTROLLER_AXIS_LEFTX,0);SDL_JoystickUpdate();assert(controller.controlsNeutral());
    SDL_Event mapping{};mapping.type=SDL_CONTROLLERDEVICEREMAPPED;mapping.cdevice.which=SDL_JoystickInstanceID(joystick);
    assert(controller.event(mapping));mapping.cdevice.which=-99;assert(!controller.event(mapping));
    sixty.close();ninety.close();
    SDL_JoystickClose(joystick);assert(SDL_JoystickDetachVirtual(index)==0);
    SDL_Event e;bool removed=false;while(SDL_PollEvent(&e))removed=controller.event(e)||removed;
    assert(removed&&!controller.connected()&&!controller.firing()&&!controller.movement().aimActive);
    index=SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_GAMECONTROLLER,SDL_CONTROLLER_AXIS_MAX,SDL_CONTROLLER_BUTTON_MAX,0);assert(index>=0);
    while(SDL_PollEvent(&e))controller.event(e);
    assert(controller.connected());controller.close();SDL_JoystickDetachVirtual(index);SDL_Quit();
}
