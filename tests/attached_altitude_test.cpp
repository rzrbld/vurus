#include "game/attached_fx_altitude.h"
#include <iostream>
#include <stdexcept>
using namespace vyruz;
static void need(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
int main(){try{
 AttachedFxAltitude c;c.initial=7;
 need(initialAttachedFxAltitude(c,30)==37,"attachment adds class and parent integer heights");
 need(initialAttachedFxAltitude(c,-30.75f)==-23,"parent is an integer runtime height");
 need(advanceAttachedFxAltitude(c,37,80,100)==37,"zero amplitude retains attachment height");
 c.onlyWhenVisible=true;need(advanceAttachedFxAltitude(c,37,-12,100)==-12,"only-visible copies current parent even without oscillation");
 c.amplitude=10;c.pulsation=.00314f;c.initial=100;
 need(advanceAttachedFxAltitude(c,37,40,0)==40,"nonzero amplitude overwrites class and retained height");
 need(advanceAttachedFxAltitude(c,37,40,100)==43,"positive sum truncation");
 need(advanceAttachedFxAltitude(c,37,-5,100)==-1,"add parent before truncating negative sum");
 need(advanceAttachedFxAltitude(c,37,-5,1100)==-8,"negative height remains signed");
 need(advanceAttachedFxAltitude(c,37,7.9f,100)==10,"port fractional parent normalizes to native integer");
 std::cout<<"PASS attached height initialization, retained/current parent, signed truncation\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
