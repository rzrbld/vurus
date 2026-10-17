#include "audio/weapon_audio_native.h"
#include <cstdlib>
#include <iostream>
using namespace vyruz;
static void check(bool okay,const char* text){if(!okay){std::cerr<<text<<'\n';std::exit(1);}}
static bool silent(NativeWeaponAudioActions a){return !a.stopChannel&&!a.stopMask&&!a.playStart&&!a.playFire&&!a.playEnd&&!a.playDry;}
int main(){
    NativeWeaponAudioInput input{true,true,true,true,true,true,true,100,true,false,false};
    NativeWeaponAudioState state;
    auto action=advanceNativeWeaponAudio(state,input);
    check(action.playStart&&!action.playFire&&!action.loop&&state.bits()==0x10000,"start-only first visit");
    input.channelActive=true;check(silent(advanceNativeWeaponAudio(state,input)),"wait for active start sample");
    input.channelActive=false;action=advanceNativeWeaponAudio(state,input);
    check(silent(action)&&state.bits()==0x20000,"start-completion visit is silent transition");
    action=advanceNativeWeaponAudio(state,input);
    check(action.playFire&&action.loop&&!action.playStart&&state.bits()==0x20000,"player fire loop on following visit");
    input.channelActive=true;check(silent(advanceNativeWeaponAudio(state,input)),"active loop must not retrigger");
    input.held=false;action=advanceNativeWeaponAudio(state,input);
    check(action.stopChannel&&!action.stopMask&&action.playEnd&&state.bits()==0,"release stops one channel then plays end");
    state={true,false};action=advanceNativeWeaponAudio(state,input);
    check(silent(action)&&state.startPlayed,"released start stage persists exactly as original");
    input.held=true;input.channelActive=false;action=advanceNativeWeaponAudio(state,input);
    check(silent(action)&&state.firePlayed,"repress after interrupted start first advances stage");
    input.fireLoop=false;input.player=false;input.channelActive=false;
    action=advanceNativeWeaponAudio(state,input);check(action.playFire&&!action.loop,"enemy never loops");
    input.held=false;input.channelActive=true;
    check(silent(advanceNativeWeaponAudio(state,input)),"nonloop fire with start waits until channel ends on release");
    input.channelActive=false;action=advanceNativeWeaponAudio(state,input);
    check(action.playEnd&&action.stopChannel&&state.bits()==0,"nonloop release after end clears stage");
    input.startAvailable=false;input.fireLoop=true;input.player=false;input.held=true;
    action=advanceNativeWeaponAudio(state,input);check(action.playFire&&!action.loop,"enemy authored loop flag still plays nonloop");
    input.held=false;input.channelActive=true;action=advanceNativeWeaponAudio(state,input);
    check(action.playEnd&&state.bits()==0,"enemy authored loop flag retains native release branch");
    input.held=true;input.ammoCount=0;input.channelActive=false;
    action=advanceNativeWeaponAudio(state,input);check(action.playDry&&state.bits()==0,"zero stock dry fire");
    input.ammoCount=-1;action=advanceNativeWeaponAudio(state,input);
    check(action.playFire&&state.firePlayed,"native negative stock is nonzero");
    input.ammoPointerPresent=false;input.channelActive=true;action=advanceNativeWeaponAudio(state,input);
    check(action.stopChannel&&action.playDry&&state.bits()==0,"absent stock resets and queries channel after stop");
    input.channelActive=true;action=advanceNativeWeaponAudio(state,input);
    check(silent(action),"absent stock waits for active dry channel when no stage bits");
    input.channelActive=false;action=advanceNativeWeaponAudio(state,input);
    check(action.playDry,"absent stock repeats dry only after channel ends");
    input.maskIsAll=true;state={true,true};action=advanceNativeWeaponAudio(state,input);
    check(silent(action)&&state.bits()==0,"all-channel absent stock clears stage but never emits dry");
    input.held=false;state={false,true};input.channelActive=false;
    action=advanceNativeWeaponAudio(state,input);
    check(action.stopMask&&!action.stopChannel&&!action.playEnd&&state.firePlayed,"absent-stock release stops whole mask without clearing bits");
    input.maskIsAll=false;action=advanceNativeWeaponAudio(state,input);
    check(silent(action)&&state.firePlayed,"absent-stock released inactive channel preserves bits");
    input.channelActive=true;action=advanceNativeWeaponAudio(state,input);
    check(action.stopMask&&state.firePlayed,"absent-stock released active loop stops route mask");
    input.ammoPointerPresent=true;input.ammoCount=0;input.maskIsAll=true;input.held=true;state={};
    action=advanceNativeWeaponAudio(state,input);
    check(action.playDry,"present zero stock can dry fire even with all-channel mask");
    input.ammoPointerPresent=true;input.maskIsAll=true;input.held=true;input.channelActive=true;input.ammoCount=100;
    action=advanceNativeWeaponAudio(state,input);
    check(action.playFire&&!action.stopChannel&&!action.loop,"all-mask fire does not query active channel and enemy does not loop");
    input.startAvailable=true;state={true,false};action=advanceNativeWeaponAudio(state,input);
    check(silent(action)&&state.firePlayed,"all-mask start advances despite active channel");
    input.held=false;input.endAvailable=false;action=advanceNativeWeaponAudio(state,input);
    check(silent(action)&&state.bits()==0,"missing end sample still clears native stage");
    input.held=true;input.ammoCount=0;input.dryAvailable=false;input.maskIsAll=false;state={};
    action=advanceNativeWeaponAudio(state,input);
    check(action.stopChannel&&!action.playDry&&state.bits()==0,"missing dry sample still stops occupied channel");
    input.startAvailable=false;input.fireAvailable=false;input.dryAvailable=true;state={};
    check(silent(advanceNativeWeaponAudio(state,input)),"missing start and fire do not independently trigger dry sample");
    input.startAvailable=true;input.ammoCount=100;input.channelActive=false;
    action=advanceNativeWeaponAudio(state,input);check(action.playStart&&state.startPlayed,"start-only authored sound");
    action=advanceNativeWeaponAudio(state,input);check(silent(action)&&state.bits()==0,"missing fire completes start without fire stage");
    for(unsigned subtype=0;subtype<96;++subtype)
        check(nativeWeaponAudioEligible(3,subtype,true,true)==(((subtype-16u)&31u)>=6),"native wrapped subtype exclusions");
    check(!nativeWeaponAudioEligible(2,22,true,true)&&!nativeWeaponAudioEligible(3,22,false,true)&&!nativeWeaponAudioEligible(3,22,true,false),"type/weapon admission");
    const auto player=nativeWeaponAudioRoute(true,0,0xffff,130);
    check(player.channel==2&&player.mask==2&&player.priority==191,"reserved player channel/priority");
    const auto enemy=nativeWeaponAudioRoute(false,10,0xffff,129);
    check(enemy.channel==10&&enemy.mask==512&&enemy.channelMask()==512&&enemy.priority==129,"enemy assigned metadata channel");
    const auto unassigned=nativeWeaponAudioRoute(false,0,0xff00,129);
    check(unassigned.channel==0&&unassigned.mask==0xff00&&unassigned.channelMask()==0x80000000u,"unassigned native channel shift semantics");
    check(nativeAudioAllChannelsMask(16)==0xffff&&nativeAudioAllChannelsMask(32)==0,"x86 all-channel mask shift");
    for(unsigned bits=0;bits<32;++bits){
        const bool held=bits&1,playerActor=bits&2,invisible=bits&4,motor=bits&8,special=bits&16;
        check(nativeWeaponAudioHeld(held,playerActor,invisible,motor,special)==(held&&!invisible&&!(playerActor&&(motor||special))),"held suppression flags");
    }
    std::cout<<"Native weapon audio stage, stock, held, channel and loop controls passed\n";
}
