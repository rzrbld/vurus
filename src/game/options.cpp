#include "platform/parse_float.h"
#include <type_traits>
#include "game/options.h"
#include "render/hud_layout.h"
#include <algorithm>
#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <locale>
#include <set>
#include <sstream>
#include <string_view>
#include <vector>
#include <fcntl.h>
#include "platform/files.h"

namespace vyruz { namespace {
constexpr size_t maxOptionsBytes=16384;
constexpr std::array<const char*,optionActionCount> actionNames={
    "Forward","Reverse","Left","Right","Fire","Next weapon","Previous weapon","Map","Inventory","Pause","Panel help","High / low speed"};
constexpr std::array<const char*,optionActionCount> actionKeys={
    "forward","reverse","left","right","fire","next_weapon","previous_weapon","map","inventory","pause","panel_help","speed_mode"};
std::string_view trim(std::string_view value) {
    constexpr std::string_view whitespace=" \t\r";
    const auto first=value.find_first_not_of(whitespace);
    return first==value.npos?std::string_view{}:value.substr(first,value.find_last_not_of(whitespace)-first+1);
}
template<class T> bool number(std::string_view value,T& out) {
    if constexpr(std::is_same_v<T,float>) return platform::parseFiniteFloat(value,out);
    else {
    T parsed{};const auto r=std::from_chars(value.data(),value.data()+value.size(),parsed);
    if(r.ec!=std::errc{}||r.ptr!=value.data()+value.size())return false;
    out=parsed;return true;
    }
}
bool number(std::string_view value,bool& out) {
    if(value=="0"){out=false;return true;}if(value=="1"){out=true;return true;}return false;
}
}
const char* optionActionName(OptionAction action) {
    const auto index=static_cast<size_t>(action);return index<actionNames.size()?actionNames[index]:"Unknown";
}
bool validateOptions(const GameOptions& o,std::string& error) {
    if(o.fxaa&&o.smaa){error="FXAA and SMAA cannot both be enabled";return false;}
    auto fail=[&](const char* message){error=message;return false;};
    // SDL display enumeration decides which resolutions to offer. The parser also
    // accepts existing/future desktop window sizes without binding to a monitor.
    if(o.width<400||o.width>16384||o.height<300||o.height>16384)return fail("Invalid video dimensions");
    if(o.windowMode<0||o.windowMode>3)return fail("Invalid display mode");
    if(o.frameCap!=0&&o.frameCap!=30&&o.frameCap!=60)return fail("Frame cap must be 0, 30 or 60");
    if(o.gamma<0||o.gamma>100)return fail("Brightness must be 0..100");
    if(o.spriteFiltering<0||o.spriteFiltering>2)return fail("Sprite filtering must be 0..2");
    if(o.hudLanguage<1||o.hudLanguage>2)return fail("Invalid HUD language");
    if(o.hudAspect<0||o.hudAspect>=int(HudAspects.size()))return fail("Invalid HUD aspect ratio");
    for(float volume:{o.effectsVolume,o.voicesVolume,o.musicVolume})
        if(!std::isfinite(volume)||volume<0||volume>1)return fail("Audio volume must be 0..1");
    if(o.audioBits!=8&&o.audioBits!=16&&o.audioBits!=32)return fail("Unsupported audio quality");
    if(o.audioRate!=11025&&o.audioRate!=22050&&o.audioRate!=48000)return fail("Unsupported audio frequency");
    if(o.audioChannels!=1&&o.audioChannels!=2)return fail("Unsupported audio channels");
    if(o.playerName.empty()||o.playerName.size()>32)return fail("Player name must contain 1..32 characters");
    for(unsigned char c:o.playerName)if(c<32||c>126)return fail("Player name must use printable ASCII");
    if(trim(o.playerName).empty())return fail("Player name cannot be blank");
    if(o.saveProfile.size()>32)return fail("Invalid save profile");
    for(unsigned char c:o.saveProfile)if(c<32||c>126)return fail("Invalid save profile");
    if(o.difficulty<0||o.difficulty>3)return fail("Difficulty must be 0..3");
    std::set<int> keys,buttons;
    for(int key:o.keyBindings){
        if(key<0||key>=512||key==41)return fail("Invalid or reserved keyboard scancode");
        if(key&&!keys.insert(key).second)return fail("Duplicate keyboard binding");
    }
    for(int button:o.controllerBindings){
        // SDL_CONTROLLER_BUTTON_MAX is 21 in SDL2; do not depend on SDL headers
        // in this persistence model. Start/B are fixed menu navigation controls.
        if(button< -1||button>=21||button==6||button==1)return fail("Invalid or reserved controller button");
        if(button>=0&&!buttons.insert(button).second)return fail("Duplicate controller binding");
    }
    if(o.fireButton<0||o.fireButton>5||o.thrustButton<0||o.thrustButton>5)return fail("Invalid mouse button");
    if(o.fireButton&&o.fireButton==o.thrustButton)return fail("Duplicate mouse binding");
    if(o.joystickMoveSensitivity<10||o.joystickMoveSensitivity>100 ||
       o.joystickAimSensitivity<10||o.joystickAimSensitivity>100)
        return fail("Joystick sensitivity must be 10..100");
    if(o.aimAssist<0||o.aimAssist>4)return fail("Aim assist must be 0..4");
    if(o.joystickDeadzone<0||o.joystickDeadzone>50)return fail("Joystick deadzone must be 0..50");
    error.clear();return true;
}
bool loadOptions(const std::string& path,GameOptions& options,std::string& error) {
    std::error_code ec;
    const bool exists=std::filesystem::exists(path,ec);
    if(ec){error="Cannot inspect options: "+ec.message();return false;}
    if(!exists){error.clear();return true;}
    std::ifstream input(path,std::ios::binary);
    if(!input){error="Cannot open options file";return false;}
    std::string bytes(maxOptionsBytes+1,'\0');input.read(bytes.data(),bytes.size());
    const auto count=input.gcount();
    if(input.bad()||count>static_cast<std::streamsize>(maxOptionsBytes)){error="Options file is unreadable or exceeds 16 KiB";return false;}
    bytes.resize(static_cast<size_t>(count));
    if(bytes.find('\0')!=bytes.npos){error="Options file contains a null byte";return false;}
    GameOptions candidate=options;
    std::istringstream lines(bytes);std::string line;size_t lineNumber=0;
    std::set<std::string> seen;
    while(std::getline(lines,line)){
        ++lineNumber;auto text=trim(line);if(text.empty()||text.front()=='#')continue;
        const auto separator=text.find('=');
        auto malformed=[&](){error="Invalid options at line "+std::to_string(lineNumber);return false;};
        if(separator==text.npos)return malformed();
        auto key=trim(text.substr(0,separator)),value=trim(text.substr(separator+1));
        if(key.empty())return malformed();
        bool known=true,valid=true;
#define FIELD(name,member) if(key==name)valid=number(value,candidate.member);else
        if(key=="version"){int version=0;valid=number(value,version)&&version==1;}
        else FIELD("video.width",width) FIELD("video.height",height) FIELD("video.window_mode",windowMode) FIELD("video.vsync",vsync) FIELD("video.frame_cap",frameCap) FIELD("video.gamma",gamma) FIELD("video.fps_counter",fpsCounter) FIELD("video.sprite_filtering",spriteFiltering) FIELD("video.vignette",vignette) FIELD("video.crt",crt) FIELD("video.crt_retro",crtRetro) FIELD("video.crt_bright",crtBright) FIELD("video.fxaa",fxaa) FIELD("video.smaa",smaa) FIELD("video.bloom",bloom) FIELD("video.sharpen",sharpen)
        FIELD("hud.language",hudLanguage)
        FIELD("video.hud_aspect",hudAspect)
        FIELD("video.shadows",shadows) FIELD("video.reactor_fx",reactorFx) FIELD("video.ground_details",groundDetails)
        FIELD("audio.effects",soundFx) FIELD("audio.voices",voices) FIELD("audio.music",music)
        FIELD("audio.menu_sounds",menuSounds)
        FIELD("audio.effects_volume",effectsVolume) FIELD("audio.voices_volume",voicesVolume)
        FIELD("audio.music_volume",musicVolume) FIELD("audio.bits",audioBits)
        FIELD("audio.rate",audioRate) FIELD("audio.channels",audioChannels)
        FIELD("session.difficulty",difficulty) FIELD("mouse.fire",fireButton) FIELD("mouse.thrust",thrustButton)
        FIELD("controller.move_sensitivity",joystickMoveSensitivity) FIELD("controller.aim_sensitivity",joystickAimSensitivity)
        FIELD("controller.deadzone",joystickDeadzone) FIELD("controller.aim_assist",aimAssist) FIELD("controls.crosshair",crosshair)
        if(key=="session.name"||key=="session.save_profile"){
            std::istringstream quoted{std::string(value)};quoted.imbue(std::locale::classic());
            valid=!value.empty()&&value.front()=='"'&&bool(quoted>>std::quoted(key=="session.name"?candidate.playerName:candidate.saveProfile));
            if(valid){quoted>>std::ws;valid=quoted.eof();}
        }else{
            known=false;
            for(size_t i=0;i<optionActionCount;++i){
                if(key==std::string("key.")+actionKeys[i]){known=true;valid=number(value,candidate.keyBindings[i]);break;}
                if(key==std::string("controller.")+actionKeys[i]){known=true;valid=number(value,candidate.controllerBindings[i]);break;}
            }
        }
#undef FIELD
        if(known&&(!valid||!seen.insert(std::string(key)).second))return malformed();
    }
    // Existing settings may already use F3/Page Down. An absent new action
    // must not invalidate or steal the user's older explicit binding.
    for(const auto action:{OptionAction::PanelHelp,OptionAction::SpeedMode}) {
        const auto id=static_cast<size_t>(action);
        if(!seen.contains(std::string("key.")+actionKeys[id])&&candidate.keyBindings[id])
            for(size_t other=0;other<optionActionCount;++other)
                if(other!=id&&candidate.keyBindings[other]==candidate.keyBindings[id]){candidate.keyBindings[id]=0;break;}
    }
    if(!validateOptions(candidate,error))return false;
    options=std::move(candidate);error.clear();return true;
}
bool saveOptions(const std::string& path,const GameOptions& options,std::string& error) {
    if(!validateOptions(options,error))return false;
    std::ostringstream text;text.imbue(std::locale::classic());text<<std::setprecision(9)<<"# Vyruz port preferences\nversion=1\n";
#define FIELD(key,member) text<<key<<'='<<options.member<<'\n';
    FIELD("video.width",width) FIELD("video.height",height) FIELD("video.window_mode",windowMode) FIELD("video.vsync",vsync) FIELD("video.frame_cap",frameCap) FIELD("video.gamma",gamma) FIELD("video.fps_counter",fpsCounter) FIELD("video.sprite_filtering",spriteFiltering) FIELD("video.vignette",vignette) FIELD("video.crt",crt) FIELD("video.crt_retro",crtRetro) FIELD("video.crt_bright",crtBright) FIELD("video.fxaa",fxaa) FIELD("video.smaa",smaa) FIELD("video.bloom",bloom) FIELD("video.sharpen",sharpen)
    FIELD("hud.language",hudLanguage)
        FIELD("video.hud_aspect",hudAspect)
    FIELD("video.shadows",shadows) FIELD("video.reactor_fx",reactorFx) FIELD("video.ground_details",groundDetails)
    FIELD("audio.effects",soundFx) FIELD("audio.voices",voices) FIELD("audio.music",music)
    FIELD("audio.menu_sounds",menuSounds)
    FIELD("audio.effects_volume",effectsVolume) FIELD("audio.voices_volume",voicesVolume)
    FIELD("audio.music_volume",musicVolume) FIELD("audio.bits",audioBits)
    FIELD("audio.rate",audioRate) FIELD("audio.channels",audioChannels)
    FIELD("session.difficulty",difficulty) FIELD("mouse.fire",fireButton) FIELD("mouse.thrust",thrustButton)
    FIELD("controller.move_sensitivity",joystickMoveSensitivity) FIELD("controller.aim_sensitivity",joystickAimSensitivity)
    FIELD("controller.deadzone",joystickDeadzone) FIELD("controller.aim_assist",aimAssist) FIELD("controls.crosshair",crosshair)
#undef FIELD
    text<<"session.name="<<std::quoted(options.playerName)<<'\n';
    text<<"session.save_profile="<<std::quoted(options.saveProfile)<<'\n';
    for(size_t i=0;i<optionActionCount;++i)
        text<<"key."<<actionKeys[i]<<'='<<options.keyBindings[i]<<'\n'<<"controller."<<actionKeys[i]<<'='<<options.controllerBindings[i]<<'\n';
    const auto bytes=text.str();
    return platform::atomicWrite(path,{reinterpret_cast<const uint8_t*>(bytes.data()),bytes.size()},error);
}
} // namespace vyruz
