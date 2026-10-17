#include "audio/input_commentary_native.h"
#include <cstdlib>
#include <cstring>
#include <iostream>
using namespace vyruz;
static void need(bool ok,const char* why){if(!ok){std::cerr<<why<<'\n';std::exit(1);}}
int main() {
    NativeInputCommentary fresh;
    need(!fresh.advance(24000,false),"idle speech requires a previous active event");
    need(fresh.lastQuiet==24000 && fresh.lastActive==0,"native initially quiet globals");
    need(!fresh.advance(24001,true),"first fire after quiet does not immediately speak");
    need(fresh.lastActive==24001 && fresh.lastQuiet==24000,"fire retains quiet origin");
    need(!fresh.advance(26999,true),"held fire below three-second boundary");
    auto first=fresh.advance(27000,true);
    need(first && std::strcmp(first->name,"V-ARIBA.WAV")==0,"three-second equality triggers residue0");
    need(first->delayMs==300 && first->holdMs==2000 && first->tailMs==0,"fire native scheduling tuple");
    need(fresh.lastQuiet==27000,"fire trigger restarts interval");
    need(!fresh.advance(27001,false),"release refreshes quiet origin");
    need(!fresh.advance(46999,false),"idle below twenty-second boundary");
    auto idle=fresh.advance(47000,false);
    need(idle && std::strcmp(idle->name,"V-WAKE-UP.WAV")==0,"idle equality triggers residue0");
    need(idle->delayMs==0 && idle->holdMs==3000 && idle->tailMs==0,"idle native scheduling tuple");
    need(fresh.lastActive==47000,"idle trigger restarts interval");
    static constexpr const char* fireNames[]={"V-ARIBA.WAV","V-YEEPEE.WAV","V-YEAH.WAV"};
    static constexpr const char* idleNames[]={"V-WAKE-UP.WAV","V-WAITING-FOR.WAV","V-WHAT-DOING.WAV","V-FORGET-MISSION.WAV"};
    for(unsigned residue=0;residue<8;++residue) {
        NativeInputCommentary fire{1000,1000};const uint32_t now=4000+residue;
        auto cue=fire.advance(now,true);
        need(bool(cue)==(residue<3),"fire selected/silent residues");
        if(cue)need(std::strcmp(cue->name,fireNames[residue])==0,"fire authored order");
        need(fire.lastQuiet==now && fire.lastActive==now,"silent fire choices reset both clocks");
        need(!fire.advance(now+1,true),"silent fire choice cannot retry next tick");
        NativeInputCommentary quiet{1000,1000};const uint32_t later=21000+residue;
        cue=quiet.advance(later,false);
        need(bool(cue)==(residue<4),"idle selected/silent residues");
        if(cue)need(std::strcmp(cue->name,idleNames[residue])==0,"idle authored order");
        need(quiet.lastQuiet==later && quiet.lastActive==later,"silent idle choices reset both clocks");
        need(!quiet.advance(later+1,false),"silent idle choice cannot retry next tick");
    }
    NativeInputCommentary wrapped{UINT32_MAX-2999,UINT32_MAX-19999};
    auto cue=wrapped.advance(0,true);
    need(cue && wrapped.lastQuiet==0 && wrapped.lastActive==0,"unsigned elapsed subtraction wraps exactly");
    need(!wrapped.advance(3000,true),"native zero sentinel retained after wrap");
    NativeInputCommentary idleWrapped{1,UINT32_MAX-19999};
    need(idleWrapped.advance(0,false).has_value(),"idle unsigned wrap equality");
    NativeInputCommentary sustained;
    need(!sustained.advance(1,true) && !sustained.advance(4000,true),"no quiet sample leaves initial zero sentinel intact");
    need(!sustained.advance(4001,false),"quiet visit initializes held-fire timer");
    std::cout<<"PASS native fire/idle commentary thresholds, choices, silent resets and uint32 wrap\n";
}
