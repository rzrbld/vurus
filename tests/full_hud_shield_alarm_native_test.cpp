#include "audio/full_hud_shield_alarm_native.h"
#include <cstdlib>
#include <iostream>
using namespace vyruz;
static void need(bool ok,const char* why){if(!ok){std::cerr<<why<<'\n';std::exit(1);}}
int main(){
    //Exact independently decoded shipped PANEL.GAF sequence1 fields.
    GafSequence sequence;sequence.flags=7;sequence.loopStart=2;
    sequence.subframes={{20,-99,0,30,1,0,0},{GafSubframe::Blank,-99,0,30,1,0,0},{21,-99,0,30,1,1,1}};
    NativeFullHudShieldAlarm alarm;alarm.reset(sequence);
    need(alarm.state().cursor==1&&alarm.state().remaining==420,"native HUD initialization retains first full frame");
    need(alarm.advance(419,5,9,1000).empty(),"first frame before boundary has no alarm");
    const auto held=alarm.state();need(alarm.advance(1000,5,11,1000).empty(),"healthy shield does not trigger animation audio");
    need(alarm.state().cursor==held.cursor&&alarm.state().remaining==held.remaining,"healthy shield preserves partial alarm phase");
    need(alarm.advance(1,5,9,1000).empty()&&alarm.state().cursor==2,"blank entered frame is counted but silent");
    need(alarm.advance(419,5,9,1000).empty(),"second frame before alarm remains silent");
    need(alarm.advance(1,5,9,1000)==std::vector<std::uint32_t>{1},"alarm at exact840ms boundary");
    need(alarm.advance(839,5,9,1000).empty(),"loop does not alarm early");
    need(alarm.advance(1,5,9,1000)==std::vector<std::uint32_t>{1},"loop2/3 repeats alarm each840ms");
    const auto phase=alarm.state();need(alarm.advance(1000,1,9,1000).empty(),"minimal HUD skips full-HUD animation");
    need(alarm.state().cursor==phase.cursor&&alarm.state().remaining==phase.remaining,"minimal HUD freezes full-HUD phase");
    alarm.reset(sequence);need(alarm.advance(2520,2,9,1000)==std::vector<std::uint32_t>({1,1,1}),"crossed frames retain every entered sample");
    alarm.reset(sequence);need(alarm.advance(840,5,10,1000).empty(),"native extended ratio honors rounded float threshold");
    need(alarm.advance(840,5,.01f,1)==std::vector<std::uint32_t>{1},"native float threshold equality qualifies");
    alarm.reset();need(alarm.advance(840,5,0,1000).empty(),"missing optional panel sequence stays silent");
    std::cout<<"PASS native full-HUD low-shield sample cadence, retained healthy phase, blank frames and crossed-frame audio\n";
}
