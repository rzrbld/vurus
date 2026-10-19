#include "game/options.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include "host_temp.h"

namespace {
void write(const std::filesystem::path& path,const std::string& bytes) {
    std::ofstream file(path,std::ios::binary);file.write(bytes.data(),bytes.size());assert(file.good());
}
std::string read(const std::filesystem::path& path) {
    std::ifstream file(path,std::ios::binary);return {std::istreambuf_iterator<char>(file),{}};
}
}
int main() {
    using namespace vyruz;
    const auto directory=hostTempDirectory("vyruz-options-test-");
    const std::filesystem::path root=directory,path=root/"settings.cfg";
    std::string error="old error";
    GameOptions original;assert(validateOptions(original,error)&&error.empty());
    assert(!original.fpsCounter && original.vsync && original.frameCap==60);
    for(int cap:{0,30,60}){auto valid=original;valid.frameCap=cap;assert(validateOptions(valid,error));}
    for(int cap:{-1,1,90}){auto invalid=original;invalid.frameCap=cap;assert(!validateOptions(invalid,error));}
    assert(!original.smaa&&!original.fxaa&&original.crosshair);
    GameOptions conflict=original;conflict.smaa=conflict.fxaa=true;assert(!validateOptions(conflict,error));
    GameOptions options=original;options.gamma=73;
    // A first launch keeps caller defaults, including explicit command-line policy.
    assert(loadOptions(path.string(),options,error)&&options.gamma==73&&error.empty());
    options.windowMode=3;options.width=1280;options.height=800;options.gamma=0;options.spriteFiltering=2;options.hudAspect=11;options.hudLanguage=2;
    options.fpsCounter=true;options.vsync=false;options.frameCap=30;
    options.fxaa=options.bloom=options.sharpen=true;options.crt=true;options.crtRetro=true;options.crtBright=true;options.vignette=false;options.shadows=false;options.reactorFx=false;options.soundFx=false;options.music=false;options.menuSounds=false;
    options.effectsVolume=.375f;options.voicesVolume=.125f;options.musicVolume=.875f;
    options.audioBits=8;options.audioRate=11025;options.audioChannels=1;
    options.crosshair=false;options.aimAssist=4;options.playerName="Pilot \"One\" \\ = #";options.difficulty=3;
    options.keyBindings[0]=82;options.controllerBindings[0]=7;
    options.fireButton=2;options.thrustButton=0;
    options.joystickMoveSensitivity=65;options.joystickAimSensitivity=40;options.joystickDeadzone=30;options.aimAssist=3;
    assert(saveOptions(path.string(),options,error));
    GameOptions loaded;
    assert(loadOptions(path.string(),loaded,error)&&loaded==options);
    options.smaa=true;options.fxaa=false;assert(saveOptions(path.string(),options,error));assert(loadOptions(path.string(),loaded,error)&&loaded==options);
    const auto validBytes=read(path);
    GameOptions invalid=options;invalid.gamma=101;
    assert(!saveOptions(path.string(),invalid,error)&&!error.empty());
    assert(read(path)==validBytes); // Validation failure never damages existing preferences.
    assert(!saveOptions((root/"missing"/"settings.cfg").string(),options,error));
    assert(!saveOptions(root.string(),options,error)); // Failed rename cleans up its temp file.
    size_t entries=0;for(const auto& entry:std::filesystem::directory_iterator(root)){(void)entry;++entries;}
    assert(entries==1);
    // Invalid complete files are rejected without partially applying earlier fields.
    for(const std::string bad:{
        "video.frame_cap=90\n", "video.frame_cap=30\nvideo.frame_cap=0\n", "video.vsync=2\n", "video.vsync=true\n", "video.vsync=0\nvideo.vsync=1\n", "video.fps_counter=2\n", "video.fps_counter=0\nvideo.fps_counter=1\n", "controls.crosshair=2\n", "controller.aim_assist=-1\n", "video.width=800\nvideo.gamma=101\n", "video.window_mode=-1\n", "video.window_mode=4\n", "video.window_mode=1.5\n", "video.window_mode=0\nvideo.window_mode=1\n", "video.fxaa=2\n", "video.bloom=true\n", "video.sharpen=1\nvideo.sharpen=0\n", "video.crt_retro=2\n", "video.crt_bright=2\n", "video.crt=2\n", "video.crt=true\n", "video.crt=1\nvideo.crt=0\n", "video.vignette=2\n", "video.vignette=true\n", "video.vignette=0\nvideo.vignette=1\n", "video.sprite_filtering=-1\n", "video.sprite_filtering=3\n",
        "hud.language=0\n", "hud.language=3\n", "hud.language=1.5\n",
        "video.hud_aspect=-1\n", "video.hud_aspect=12\n", "video.hud_aspect=1.5\n", "video.hud_aspect=1\nvideo.hud_aspect=2\n",
        "audio.effects_volume=nan\n", "audio.effects_volume=inf\n", "audio.effects_volume=1.1\n",
        "video.width=800px\n", "video.width=999999999999999999999\n",
        "video.gamma=1\nvideo.gamma=2\n", "version=2\n",
        "audio.effects=true\n", "audio.menu_sounds=2\n", "audio.menu_sounds=true\n", "audio.menu_sounds=0\naudio.menu_sounds=1\n", "audio.rate=44100\n", "audio.bits=24\n",
        "audio.channels=0\n", "session.difficulty=4\n", "session.name=unquoted\n",
        "session.name=\"ok\" junk\n", "session.name=\"unfinished\n", "session.name=\"\"\n",
        "session.name=\"   \"\n", "key.forward=41\n", "key.forward=512\n",
        "key.forward=22\n", "controller.fire=1\n", "controller.fire=6\n",
        "controller.fire=21\n", "controller.fire=3\n", "mouse.fire=3\n",
        "mouse.fire=6\n", "controller.move_sensitivity=9\n", "controller.move_sensitivity=101\n",
        "controller.aim_sensitivity=9\n", "controller.aim_sensitivity=101\n",
        "controller.aim_assist=5\n", "controller.deadzone=-1\n", "controller.deadzone=51\n", "controller.deadzone=22.5\n",
        "bad line\n", "=bad\n"}) {
        write(path,bad);loaded=original;
        assert(!loadOptions(path.string(),loaded,error)&&loaded==original&&!error.empty());
    }
    for(const std::string& bad:{std::string(16385,'#'),std::string("video.gamma=20\0\n",16),
                               "session.name=\""+std::string(33,'a')+"\"\n"}) {
        write(path,bad);loaded=original;
        assert(!loadOptions(path.string(),loaded,error)&&loaded==original);
    }
    write(path,"version=1\naudio.effects=1\n");loaded=original;
    assert(loadOptions(path.string(),loaded,error)&&loaded.crosshair&&loaded.menuSounds&&loaded.hudAspect==0&&loaded.windowMode==0);
    // Legacy configs keep their existing folder; display renames retain it.
    write(path,"session.name=\"Old Pilot\"\n");loaded=original;
    assert(loadOptions(path.string(),loaded,error)&&loaded.storageProfile()=="Old Pilot");
    loaded.saveProfile=loaded.storageProfile();loaded.playerName="New Pilot";
    assert(saveOptions(path.string(),loaded,error));
    GameOptions renamed;assert(loadOptions(path.string(),renamed,error));
    assert(renamed.playerName=="New Pilot"&&renamed.storageProfile()=="Old Pilot");
    // Unknown keys are ignored, including future duplicates; known keys remain strict.
    write(path,"# comment\r\nversion=1\r\n future.option = arbitrary data\nfuture.option=other\nvideo.gamma = 100\n");
    loaded=original;assert(loadOptions(path.string(),loaded,error)&&loaded.gamma==100&&error.empty());
    loaded=original;loaded.effectsVolume=std::numeric_limits<float>::quiet_NaN();
    assert(!validateOptions(loaded,error));
    loaded=original;loaded.playerName="bad\nname";assert(!validateOptions(loaded,error));
    loaded=original;loaded.keyBindings.fill(0);loaded.controllerBindings.fill(-1);
    loaded.fireButton=loaded.thrustButton=0;
    assert(validateOptions(loaded,error)); // Multiple unbound controls are valid.
    assert(original.joystickMoveSensitivity==100&&original.joystickAimSensitivity==100&&original.joystickDeadzone==22);
    assert(original.controllerBindings[0]==11&&original.controllerBindings[1]==12&&
           original.controllerBindings[2]==13&&original.controllerBindings[3]==14&&original.controllerBindings[4]==0);
    loaded=original;loaded.joystickMoveSensitivity=10;loaded.joystickAimSensitivity=10;loaded.joystickDeadzone=0;
    assert(validateOptions(loaded,error));
    loaded.joystickMoveSensitivity=100;loaded.joystickAimSensitivity=100;loaded.joystickDeadzone=50;
    assert(validateOptions(loaded,error));
    assert(std::string(optionActionName(OptionAction::Forward))=="Forward");
    assert(std::string(optionActionName(OptionAction::Count))=="Unknown");
    std::filesystem::remove_all(root);
}
