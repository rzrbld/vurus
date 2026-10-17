#pragma once
#include <cstdint>

namespace vyruz {
// 43df70 visits World+2010 (the live awake sparse vector), not shot events.
// The caller must preserve that ordering and supply the *current* weapon/stock.
constexpr bool nativeWeaponAudioEligible(unsigned type,unsigned subtype,
                                        bool weaponInstance,bool weaponDetails){
    return type==3 && (((subtype-0x10u)&31u)>=6u) && weaponInstance && weaponDetails;
}
constexpr bool nativeWeaponAudioHeld(bool held,bool player,bool invisible,
                                     bool motorBlocked,bool specialPlayerWeapon){
    return held&&!invisible&&!(player&&(motorBlocked||specialPlayerWeapon));
}
struct NativeWeaponAudioRoute {
    unsigned channel=0;
    std::uint32_t mask=0,priority=0;
    constexpr std::uint32_t channelMask()const{return std::uint32_t(1)<<((channel-1u)&31u);}
};
constexpr NativeWeaponAudioRoute nativeWeaponAudioRoute(bool player,unsigned metadataChannel,
                                                        std::uint32_t classMask,std::uint32_t classPriority){
    if(player)return {2,2,0xbf};
    return {metadataChannel,metadataChannel?std::uint32_t(1)<<((metadataChannel-1u)&31u):classMask,classPriority};
}
constexpr std::uint32_t nativeAudioAllChannelsMask(unsigned channels){
    return (std::uint32_t(1)<<(channels&31u))-1u; // x86 SHL count semantics.
}
struct NativeWeaponAudioState {
    bool startPlayed=false,firePlayed=false; // actor+128 bits10000/20000.
    constexpr std::uint32_t bits()const{return(startPlayed?0x10000u:0u)|(firePlayed?0x20000u:0u);}
};
struct NativeWeaponAudioInput {
    bool startAvailable=false,fireAvailable=false,endAvailable=false,dryAvailable=false;
    bool fireLoop=false,player=false;
    bool ammoPointerPresent=true;
    int ammoCount=0; // Native tests exactly zero; do not replace by <=0.
    bool held=false,channelActive=false,maskIsAll=false;
};
struct NativeWeaponAudioActions {
    // Most native stops address one channel, not the route's whole class mask.
    // The absent-stock/released-loop branch alone stops the whole route mask.
    bool stopChannel=false,stopMask=false;
    bool playStart=false,playFire=false,playEnd=false,playDry=false;
    bool loop=false; // Fire-loop request is admitted only for the player.
};
constexpr NativeWeaponAudioActions advanceNativeWeaponAudio(NativeWeaponAudioState& state,
                                                            NativeWeaponAudioInput in){
    NativeWeaponAudioActions out;
    if(!in.ammoPointerPresent){
        if(!in.held){
            if(state.firePlayed&&in.fireLoop&&(in.maskIsAll||in.channelActive))out.stopMask=true;
        } else {
            bool active=in.channelActive;
            if(state.startPlayed||state.firePlayed){
                state={};
                if(!in.maskIsAll&&active){out.stopChannel=true;active=false;}
            }
            // 43e2fd queries again *after* the preceding stop.
            if(!in.maskIsAll&&!active)out.playDry=in.dryAvailable;
        }
        return out;
    }
    if(!in.held){
        const bool endLoop=in.fireAvailable&&state.firePlayed&&in.fireLoop;
        const bool endStarted=in.startAvailable&&state.firePlayed&&(in.maskIsAll||!in.channelActive);
        if(endLoop||endStarted){
            state={};out.stopChannel=!in.maskIsAll;out.playEnd=in.endAvailable;
        }
        return out;
    }
    if(in.startAvailable&&!state.firePlayed){
        if(!state.startPlayed){
            if(in.ammoCount==0)out.playDry=in.dryAvailable;
            else {state.startPlayed=true;state.firePlayed=false;out.playStart=true;}
            out.stopChannel=!in.maskIsAll&&in.channelActive;
        } else if(in.maskIsAll||!in.channelActive){
            // This visit advances the stage only. Fire playback follows on a
            // subsequent visit, even though firePlayed is already set here.
            state.startPlayed=false;
            if(in.fireAvailable)state.firePlayed=true;
        }
        return out;
    }
    if(in.fireAvailable&&(!state.firePlayed||in.maskIsAll||!in.channelActive)){
        if(in.ammoCount==0)out.playDry=in.dryAvailable;
        else {
            state.startPlayed=false;state.firePlayed=true;
            out.playFire=true;out.loop=in.player&&in.fireLoop;
        }
        out.stopChannel=!in.maskIsAll&&in.channelActive;
    }
    return out;
}
}
