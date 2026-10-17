#include "audio/shield_alarm_native.h"
#include <cstdlib>
#include <iostream>
using namespace vyruz;
static void need(bool ok,const char* why){if(!ok){std::cerr<<why<<'\n';std::exit(1);}}
int main(){
    NativeShieldAlarm alarm;
    need(!alarm.advance(14,5,0,1000)&&alarm.remainingMs==0,"HUD detail mode5 freezes alarm timer");
    need(alarm.advance(14,1,9,1000)&&alarm.remainingMs==1000,"low shield starts immediate alarm at due timer");
    need(!alarm.advance(999,0,9,1000)&&alarm.remainingMs==1,"alarm must wait below cadence boundary");
    need(!alarm.advance(500,2,9,1000)&&alarm.remainingMs==1,"HUD detail mode2 retains remaining interval");
    need(alarm.advance(1,0,9,1000)&&alarm.remainingMs==1000,"alarm repeats at exact1000ms boundary");
    need(!alarm.advance(1000,1,11,1000)&&alarm.remainingMs==0,"above one percent suppresses due alarm");
    need(!alarm.advance(14,1,11,1000)&&alarm.remainingMs==-14,"healthy visits retain negative countdown");
    need(alarm.advance(14,1,9,1000)&&alarm.remainingMs==1000,"shield drop after elapsed healthy interval alarms immediately");
    need(alarm.advance(5000,1,0,1000)&&alarm.remainingMs==1000,"large update emits one alarm, no catchup burst");
    NativeShieldAlarm boundary;
    need(boundary.advance(0,1,.01f,1),"authored float threshold equality accepted");
    boundary={};need(!boundary.advance(0,1,10,1000),"native extended ratio may exceed authored rounded float threshold");
    boundary={};need(boundary.advance(0,1,0,0),"native unordered ratio enters alarm branch");
    boundary.remainingMs=INT32_MIN;
    need(!boundary.advance(1,0,0,1000)&&boundary.remainingMs==INT32_MAX,"countdown signed word wraps with x86 subtraction");
    std::cout<<"PASS native shield alarm mode gating, countdown, cadence, threshold and x87 unordered path\n";
}
