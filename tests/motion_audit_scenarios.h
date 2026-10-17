#pragma once
#include "game/controls.h"
#include <string_view>
inline constexpr std::string_view motionAuditCases[]={"accelerate-coast","reverse","strafe-left","strafe-right","diagonal","aim-only"};
inline vyruz::MovementCommand motionAuditInput(std::string_view scenario,int tick,int heading){
 vyruz::MovementCommand command;
 if(scenario=="accelerate-coast")command.forward=tick<300?1:0;
 else if(scenario=="reverse")command.forward=tick<300?1:-1;
 else if(scenario=="strafe-left")command.strafe=-1;
 else if(scenario=="strafe-right")command.strafe=1;
 else if(scenario=="diagonal"){command.forward=1;command.strafe=1;}
 else if(scenario=="aim-only"){
  command.aimActive=true;const auto h=vyruz::nativeHeading((heading+6)%24);
  const auto aim=vyruz::nativeToSurface(h.x,h.y);command.aimX=aim.x;command.aimY=aim.y;
 }
 return command;
}
