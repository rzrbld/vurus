// Application-side options: the model and persistence are in game/options.*.
std::string defaultOptionsPath() { return portableUserPath("options.cfg"); }
std::string legacyOptionsPath() {
    if(const char* xdg=std::getenv("XDG_CONFIG_HOME");xdg&&*xdg)return std::string(xdg)+"/vyruz26/options.cfg";
    if(const char* home=std::getenv("HOME");home&&*home)return std::string(home)+"/.config/vyruz26/options.cfg";
    return "options.cfg";
}
bool applyOptionsRuntime(App& a,const vyruz::GameOptions& o,std::string& error,bool resizeWindow=true,bool* displayFailure=nullptr) {
    if(displayFailure)*displayFailure=false;
    if(a.audio.device().available() && !a.audio.device().configure({o.audioRate,o.audioBits,o.audioChannels},error))return false;
    a.audio.device().setEffectsVolume(o.soundFx?o.effectsVolume:0);
    a.audio.device().setVoicesVolume(o.voices?o.voicesVolume:0);
    a.audio.device().setMusicVolume(o.musicVolume);
    a.audio.setMusicOption(o.music);
    a.controller.configure(o.joystickMoveSensitivity,o.joystickAimSensitivity,o.joystickDeadzone);
    a.gfx.setGamma(o.gamma);a.gfx.setSpriteFiltering(o.spriteFiltering);a.sceneRenderer.reactorFx=o.reactorFx;a.sceneRenderer.groundDetails=o.groundDetails;a.sceneRenderer.shadows=o.shadows;
    a.gfx.setHudAspect(o.hudAspect);
    a.hud.setLanguage(o.hudLanguage);a.world.setHudLanguage(o.hudLanguage);
    if(!vyruz::applyWindowVideo(a.window,o.windowMode,o.width,o.height,resizeWindow,a.windowedBounds,error)){if(displayFailure)*displayFailure=true;return false;}
    int w=0,h=0;SDL_GetWindowSize(a.window,&w,&h);a.gfx.resize(w,h);
    // Preserve startup's software fallback on displays without swap control;
    // disabling must actually succeed; enabling may retain the software cap.
    if(!a.gfx.setVsync(o.vsync,error) && !o.vsync)return false;
#if VYRUZ_GLES
    // A copied desktop config must not prevent handheld startup or audio edits.
    if(!a.gfx.setCRT(false,error)||!a.gfx.setSceneEffects(false,false,false,error,false))return false;
#else
    a.gfx.setRetroCRT(o.crtRetro,o.crtBright);
    if(!a.gfx.setCRT(o.crt,error))return false;
    if(!a.gfx.setSceneEffects(o.fxaa,o.bloom,o.sharpen,error,o.smaa))return false;
    #endif
    error.clear();return true;
}
bool commitOptions(App& a,vyruz::GameOptions next) {
    std::string error;
    next.saveProfile=a.options.storageProfile();
    if(!vyruz::validateOptions(next,error)){a.menu.error=error;return false;}
    const bool resizeWindow=next.width!=a.options.width||next.height!=a.options.height;
    int oldWidth,oldHeight;SDL_GetWindowSize(a.window,&oldWidth,&oldHeight);
    const auto windowBefore=vyruz::captureWindowVideo(a.window,a.windowedBounds);
    auto rollback=a.options;rollback.width=oldWidth;rollback.height=oldHeight;
    if(!applyOptionsRuntime(a,next,error,resizeWindow)){std::string ignored;vyruz::restoreWindowVideo(a.window,windowBefore,a.windowedBounds);applyOptionsRuntime(a,rollback,ignored,false);a.menu.error="Setting unavailable: "+error;return false;}
    std::error_code ec;const auto parent=std::filesystem::path(a.optionsPath).parent_path();
    if(!parent.empty())std::filesystem::create_directories(parent,ec);
    if(ec || !vyruz::saveOptions(a.optionsPath,next,error)) {
        std::string ignored;vyruz::restoreWindowVideo(a.window,windowBefore,a.windowedBounds);applyOptionsRuntime(a,rollback,ignored,false);
        a.menu.error="Could not save options";return false;
    }
    if(next.windowMode!=a.options.windowMode)a.resolutions.clear();
    a.options=std::move(next);a.world.setPlayerName(a.options.playerName);a.menu.error.clear();return true;
}
std::string optionPercent(float value){return std::to_string(int(std::lround(value*100)));}
const char* optionButtonName(int button) {
    static const char* names[]={"A","B","X","Y","Back","Guide","Start","LS click","RS click","LB","RB","D-pad Up","D-pad Down","D-pad Left","D-pad Right","Misc","Paddle 1","Paddle 2","Paddle 3","Paddle 4","Touchpad"};
    return button>=0&&button<int(std::size(names))?names[button]:"Unbound";
}
const char* optionMouseName(int button) {
    switch(button){case SDL_BUTTON_LEFT:return "Left";case SDL_BUTTON_RIGHT:return "Right";
    case SDL_BUTTON_MIDDLE:return "Middle";case SDL_BUTTON_X1:return "X1";case SDL_BUTTON_X2:return "X2";default:return "Unbound";}
}
int wheelSteps(const SDL_MouseWheelEvent& event){return event.direction==SDL_MOUSEWHEEL_FLIPPED?-event.y:event.y;}
std::string optionKeyName(int value){
    if(!value)return "Unbound";
    const char* name=SDL_GetScancodeName(static_cast<SDL_Scancode>(value));
    return name&&*name?name:"Key #"+std::to_string(value);
}
void refreshOptions(App& a) {
    auto& m=a.menu;const auto& o=a.options;const auto on=[](bool b){return b?"On":"Off";};
    m.optionRowHeight=a.optionsPage==0?vyruz::MenuView::RowHeight:30;
    m.optionHelp.clear();
    switch(a.optionsPage) {
    case 0:m.optionTitle="Options";m.optionLabels={"Video","Audio","Control","Session","Back"};
        m.optionHelp={"Resolution, brightness and visual detail.","Music, Sound FX, Quality, Frequency, Mono/Stereo.","Input device, Key settings.","User name, Game difficulty level.",""};break;
    case 1: {
        m.optionTitle="Video";m.optionLabels={"Resolution: "+std::to_string(a.gfx.windowWidth())+" x "+std::to_string(a.gfx.windowHeight()),
            "Brightness: "+std::to_string(o.gamma),"Effects","Frame cap: "+(o.frameCap?std::to_string(o.frameCap):std::string("Uncapped")),"Advanced","Reset video","Back"};
        m.optionHelp={"","Adjust midtones. 50 is neutral; black and white stay unchanged.","Reactor FX, ground details/smoke and shadows.","30 / 60 / Uncapped. Gameplay stays at 60 Hz. VSync may limit uncapped rendering; --fps overrides this setting.","Display mode, HUD aspect, filtering and VSync.","",""};break;
    }
    case 2:
        m.optionTitle="Audio";m.optionLabels={"Sound FX: "+std::string(on(o.soundFx)),"Voices: "+std::string(on(o.voices)),"Music: "+std::string(on(o.music)),
            "Quality: "+std::to_string(o.audioBits)+" bit","Frequency: "+std::to_string(o.audioRate)+" Hz",
            "Channels: "+std::string(o.audioChannels==1?"Mono":"Stereo"),"Advanced","Back"};
        m.optionHelp={"Sound FX switch and volume.","Voice switch and volume.","Music switch and volume.","","","","Menu sounds and audio reset.",""};break;
    case 8: {
        std::string aspect(vyruz::HudAspects[o.hudAspect].name);
        if(o.hudAspect==0){const float actual=float(a.gfx.windowWidth())/std::max(1,a.gfx.windowHeight());const auto closest=std::min_element(vyruz::HudAspects.begin()+1,vyruz::HudAspects.end(),[&](const auto& l,const auto& r){return std::abs(actual-float(l.numerator)/l.denominator)<std::abs(actual-float(r.numerator)/r.denominator);});aspect+=" ("+(std::abs(actual-float(closest->numerator)/closest->denominator)<actual*.03f?std::string(closest->name):std::string("window"))+")";}
        m.optionTitle="Advanced video";m.optionLabels={"Mode: "+std::string(o.windowMode==0?"Windowed":o.windowMode==1?"Fullscreen":o.windowMode==2?"Borderless fullscreen":"Borderless window"),"HUD aspect: "+aspect,
            "Sprite filtering: "+std::string(o.spriteFiltering==0?"Off":o.spriteFiltering==1?"Bilinear":"Trilinear"),"VSync: "+std::string(on(o.vsync)),"Scene effects","FPS counter: "+std::string(on(o.fpsCounter)),"Reset advanced","Back"};
        m.optionHelp={"Windowed / Fullscreen / Borderless fullscreen (desktop resolution) / Borderless window.","Auto follows the window. Manual ratios change only the HUD; camera and backgrounds stay unchanged.","Smooth world sprites. Off keeps sharp pixels. Trilinear also filters reduced sprites.","Synchronize frames with the display. Off uses the software render cap.","Screen shaders, vignette, antialiasing, bloom and sharpening.","Show rendered frames per second, averaged over half a second.","",""};break;
    }
    case 16:m.optionTitle="Effects";m.optionLabels={"Reactor FX: "+std::string(on(o.reactorFx)),"Ground / smoke: "+std::string(on(o.groundDetails)),"Shadows: "+std::string(on(o.shadows)),"Back"};m.optionHelp={"Vehicle reactor effects.","Ground details and smoke.","Actor shadows.",""};break;
    case 14:m.optionTitle="Scene effects";m.optionLabels={"FXAA: "+std::string(on(o.fxaa)),"SMAA: "+std::string(on(o.smaa)),"Subtle bloom: "+std::string(on(o.bloom)),"Gentle sharpening: "+std::string(on(o.sharpen)),"Screen shader: "+std::string(!o.crt?"Off":o.crtBright?"Bright":o.crtRetro?"Retro":"CRT"),"Vignette: "+std::string(on(o.vignette)),"Reset effects","Back"};m.optionHelp={"Smooth jagged scene edges. May soften fine detail.","SMAA 1x Medium. Disables FXAA; requires OpenGL 3.0.","Soft glow around bright scene highlights.","Mild local contrast with bounded halos.","Off / CRT / Retro / Bright. Retro is warm; Bright adds a soft highlight glow.","Original shaded screen edges. The HUD remains clear.","Restore scene effect defaults.",""};
#if defined(VYRUZ_ENABLE_SMAA) && !VYRUZ_ENABLE_SMAA
        m.optionLabels[1]="SMAA: unavailable";m.optionHelp[1]="Unavailable in this platform build. Use FXAA instead.";
#endif
        break;
    case 13:m.optionTitle="Display mode";m.optionLabels={"Windowed","Fullscreen (exclusive)","Borderless fullscreen","Borderless window","Back"};m.optionHelp={"Resizable window with borders.","Uses a display-supported resolution.","Uses the desktop resolution.","Window without borders, at the selected size.",""};break;
    case 9:m.optionTitle="Advanced audio";m.optionLabels={"Menu sounds: "+std::string(on(o.menuSounds)),"Reset audio","Back"};m.optionHelp={"Hover and selection sounds in menus.","Restore all audio defaults.",""};break;
    case 10:case 11:case 12:{
        const bool enabled=a.optionsPage==10?o.soundFx:a.optionsPage==11?o.voices:o.music;
        const float volume=a.optionsPage==10?o.effectsVolume:a.optionsPage==11?o.voicesVolume:o.musicVolume;
        m.optionTitle=a.optionsPage==10?"Sound FX":a.optionsPage==11?"Voices":"Music";
        m.optionLabels={"Enabled: "+std::string(on(enabled)),"Volume: "+optionPercent(volume),"Back"};
        m.optionHelp={enabled?"Select to disable this audio source.":"Select to enable this audio source.",enabled?"":"This audio source is disabled.",""};break;
    }
    case 3:m.optionTitle="Control";m.optionLabels={"Keyboard","Mouse","Joystick / controller","Sensitivity",std::string("Aim assist: ")+std::array{"Off","Slight","Medium","Full","Retaliate"}[o.aimAssist],"Crosshair: "+std::string(on(o.crosshair)),"Restore default controls","Back"};m.optionHelp={"","","","Controller movement, aiming and stick deadzone.","Controller only: Slight/Medium help near enemies; Full selects with stick; Retaliate targets the last enemy to hurt you with a projectile.","Show the gameplay crosshair for mouse and controller.","",""};break;
    case 15:m.optionTitle="Sensitivity";m.optionLabels={"Move sensitivity: "+std::to_string(o.joystickMoveSensitivity),"Aim sensitivity: "+std::to_string(o.joystickAimSensitivity),"Stick deadzone: "+std::to_string(o.joystickDeadzone),"Back"};m.optionHelp={"Controller movement sensitivity.","Controller aiming sensitivity.","Ignore small stick movements near the center.",""};break;
    case 4: {
        static const char* levels[]={"Easy","Normal","Hard","Insane"};
        m.optionTitle="Session";m.optionLabels={"Player: "+o.playerName,"Difficulty: "+std::string(levels[o.difficulty]),std::string("HUD language: ")+(o.hudLanguage==2?"French":"English"),"Back"};
        // Name editing is a single-profile port UI rather than the native list.
        static const char* difficultyHelp[]={"For Beginners Only !!!","A taste of Hell.","Hurt me !","Control Alt Del"};
        m.optionHelp={"Set player name.",std::string("Select to change difficulty level.\n")+difficultyHelp[o.difficulty],"HUD, inventory and key pickup text; voices and menus keep their current language.",""};break;
    }
    case 5:case 6:
        m.optionTitle=a.optionsPage==5?"Keyboard":"Joystick buttons";m.optionLabels.clear();
        for(size_t i=0;i<vyruz::optionActionCount;++i) {
            const int value=a.optionsPage==5?o.keyBindings[i]:o.controllerBindings[i];
            const std::string label=a.optionsPage==5?optionKeyName(value):optionButtonName(value);
            m.optionLabels.push_back(std::string(vyruz::optionActionName(static_cast<vyruz::OptionAction>(i)))+": "+label);
        }
        m.optionLabels.push_back("Back");
        m.optionHelp.resize(m.optionLabels.size());
        m.optionHelp[static_cast<size_t>(vyruz::OptionAction::Fire)]=a.optionsPage==6?"Right trigger also fires.":"";
        if(a.optionsPage==5)for(int i=0;i<4;++i)m.optionHelp[i]="Arrow aliases work unless assigned to another action.";
        break;
    case 7:m.optionTitle="Mouse";m.optionLabels={std::string("Fire: ")+optionMouseName(o.fireButton),std::string("Forward: ")+optionMouseName(o.thrustButton),"Back"};break;
    }
#if VYRUZ_GLES
    if(a.optionsPage==14){m.optionLabels[4]="Screen shader: unavailable";m.optionHelp[4]="Post-processing is disabled in this handheld build.";}
    if(a.optionsPage==14)for(int i=0;i<4;++i){m.optionLabels[i]=std::string(i==0?"FXAA":i==1?"SMAA":i==2?"Subtle bloom":"Gentle sharpening")+": unavailable";m.optionHelp[i]="Post-processing is disabled in this handheld build.";}
#endif
    if(a.optionsPage==8 && o.vsync && !a.gfx.vsyncEnabled())
        m.optionHelp[3]="VSync is unavailable on this display; the software render cap is active.";
    m.selected=std::clamp(m.selected,0,int(m.optionLabels.size())-1);
}
void refreshMenuContext(App& a) {
    if(a.menu.page==vyruz::MenuPage::Benchmark) {
        if(!a.benchmarkReportChecked){a.lastBenchmark=vyruz::benchmark::latestReport(std::filesystem::path(binaryDirectory())/"benchmark-results");a.benchmarkReportChecked=true;}
        a.menu.benchmarkLastAvailable=a.lastBenchmark.has_value();
        a.menu.benchmarkLastInfo=a.lastBenchmark?a.lastBenchmark->report.startedUtc+"\n"+
            (a.lastBenchmark->report.complete?"COMPLETE":"INCOMPLETE"):std::string{};
        static const char* filters[]={"NEAREST","BILINEAR","TRILINEAR"};
        a.menu.benchmarkVideoInfo=std::to_string(a.options.width)+" X "+std::to_string(a.options.height)+
            "\n"+filters[std::clamp(a.options.spriteFiltering,0,2)]+" FILTERING\nSAVED VIDEO EFFECTS";
    }else a.benchmarkReportChecked=false;
    static const char* difficulties[]={"Easy","Normal","Hard","Insane"};
    const auto difficulty=[&](int value){return difficulties[std::clamp(value,0,3)];};
    refreshSaveCatalog(a);
    if(a.menu.page==vyruz::MenuPage::Main && !a.sessionStarted) {
        recoverRetrySession(a);a.menu.retryAvailable=a.restartLevel.has_value() && a.restartProfile==a.options.storageProfile();
    }
    a.menu.controlsHelp.clear();
    for(size_t i=0;i<vyruz::optionActionCount;++i)
        a.menu.controlsHelp+=std::string(vyruz::optionActionName(static_cast<vyruz::OptionAction>(i)))+": "+optionKeyName(a.options.keyBindings[i])+" / "+optionButtonName(a.options.controllerBindings[i])+"\n";
    a.menu.controlsHelp+=std::string("Mouse: aim / ")+optionMouseName(a.options.fireButton)+" fire / "+optionMouseName(a.options.thrustButton)+" forward\nPad: LS move / RS aim / RT fire\nWheel: weapon; arrows: movement aliases\nEsc or Start/B: menu"+(a.debugEnabled?std::string("; F2: debug"):std::string{});
    // Requested QoL extension: original Start New Game has no help string.
    a.menu.newGameHelp=std::string("Tower 1, level 1\nDifficulty : ")+difficulty(a.options.difficulty);
    a.menu.retryHelp.clear();
    if(a.restartLevel) {
        const auto& checkpoint=a.restartLevel->world;
        if(checkpoint.player<checkpoint.actors.size()) {
            const auto& vitals=checkpoint.actors[checkpoint.player].vitals;
            const auto seconds=uint64_t(std::max(0.0,checkpoint.elapsedMs)/1000);
            char detail[192];
            std::snprintf(detail,sizeof(detail),"   (%llu:%llu:%llu)\nEnergy : %d\nShield : %d\nDifficulty : %s ...\n",
                static_cast<unsigned long long>(seconds/3600),static_cast<unsigned long long>((seconds/60)%60),static_cast<unsigned long long>(seconds%60),
                int(vitals.energy),int(vitals.shield),difficulty(checkpoint.difficulty));
            a.menu.retryHelp=retryLevelTitle(a)+detail;
        }
    }
}
void optionsBack(App& a) {
    a.menuHold.reset();
    if(a.bindingCapture){a.bindingCapture=0;SDL_StopTextInput();a.menu.error.clear();refreshOptions(a);return;}
    if(a.optionsPage==0)a.menu.back(a.paused);
    else {a.optionsPage=a.optionsPage==16?1:a.optionsPage==15?3:(a.optionsPage==13||a.optionsPage==14)?8:a.optionsPage==8?1:a.optionsPage>=9?2:a.optionsPage>=5?3:0;a.menu.selected=0;refreshOptions(a);}
    a.suppressFireUntilRelease=true;
}
template<size_t N> int nextOption(int current,const int (&values)[N],int step) {
    auto found=std::find(std::begin(values),std::end(values),current);
    int index=found==std::end(values)?0:int(found-std::begin(values));
    return values[(index+step+int(N))%N];
}
void changeOption(App& a,int step=1) {
    const int row=a.menu.selected;auto next=a.options;const vyruz::GameOptions defaults;
    if(a.optionsPage==0) {
        if(row==4){optionsBack(a);return;}
        a.optionsPage=row+1;a.menu.selected=0;refreshOptions(a);return;
    }
    if(a.optionsPage==1) {
        if(row==6){optionsBack(a);return;}
        if(row==2){a.optionsPage=16;a.menu.selected=0;refreshOptions(a);return;}
        if(row==4){a.optionsPage=8;a.menu.selected=0;refreshOptions(a);return;}
        if(row==0) {
            if(next.windowMode==2){a.menu.error="Borderless fullscreen uses the desktop resolution";return;}
            if(a.resolutions.empty()) {
                if(next.windowMode!=1)a.resolutions.assign(vyruz::VideoModes.begin(),vyruz::VideoModes.end());
                const int display=SDL_GetWindowDisplayIndex(a.window);
                for(int i=0;i<SDL_GetNumDisplayModes(display);++i){SDL_DisplayMode mode{};
                    if(SDL_GetDisplayMode(display,i,&mode)==0&&vyruz::offeredVideoMode(mode.w,mode.h))a.resolutions.emplace_back(mode.w,mode.h);}
                a.resolutions.emplace_back(next.width,next.height);
                std::sort(a.resolutions.begin(),a.resolutions.end());a.resolutions.erase(std::unique(a.resolutions.begin(),a.resolutions.end()),a.resolutions.end());
            }
            a.resolutions.emplace_back(next.width,next.height);
            a.resolutions.emplace_back(a.gfx.windowWidth(),a.gfx.windowHeight());
            std::sort(a.resolutions.begin(),a.resolutions.end());a.resolutions.erase(std::unique(a.resolutions.begin(),a.resolutions.end()),a.resolutions.end());
            auto found=std::find(a.resolutions.begin(),a.resolutions.end(),std::pair{a.gfx.windowWidth(),a.gfx.windowHeight()});
            const int index=found==a.resolutions.end()?0:int(found-a.resolutions.begin());
            const auto mode=a.resolutions[(index+step+int(a.resolutions.size()))%a.resolutions.size()];next.width=mode.first;next.height=mode.second;
        } else if(row==1)next.gamma=std::clamp(next.gamma+step*5,0,100);
        else if(row==3)next.frameCap=nextOption(next.frameCap,{30,60,0},step);
        else {next.frameCap=defaults.frameCap;next.vsync=defaults.vsync;next.windowMode=defaults.windowMode;next.shadows=defaults.shadows;next.width=defaults.width;next.height=defaults.height;next.gamma=defaults.gamma;next.fpsCounter=defaults.fpsCounter;next.vignette=defaults.vignette;next.crt=defaults.crt;next.crtRetro=defaults.crtRetro;next.crtBright=defaults.crtBright;next.fxaa=defaults.fxaa;next.smaa=defaults.smaa;next.bloom=defaults.bloom;next.sharpen=defaults.sharpen;next.spriteFiltering=defaults.spriteFiltering;next.hudAspect=defaults.hudAspect;next.reactorFx=defaults.reactorFx;next.groundDetails=defaults.groundDetails;}
    } else if(a.optionsPage==16){
        if(row==3){optionsBack(a);return;}
        if(row==0)next.reactorFx=!next.reactorFx;
        else if(row==1)next.groundDetails=!next.groundDetails;
        else if(row==2)next.shadows=!next.shadows;
    } else if(a.optionsPage==8){
        if(row==7){optionsBack(a);return;}
        if(row==5)next.fpsCounter=!next.fpsCounter;
        else if(row==4){a.optionsPage=14;a.menu.selected=0;refreshOptions(a);return;}
        else if(row==0){a.optionsPage=13;a.menu.selected=next.windowMode;refreshOptions(a);return;}
        else if(row==1)next.hudAspect=(next.hudAspect+step+int(vyruz::HudAspects.size()))%int(vyruz::HudAspects.size());
        else if(row==2)next.spriteFiltering=nextOption(next.spriteFiltering,{0,1,2},step);
        else if(row==3)next.vsync=!next.vsync;
        else {next.vsync=defaults.vsync;next.fpsCounter=defaults.fpsCounter;next.windowMode=defaults.windowMode;next.hudAspect=defaults.hudAspect;next.spriteFiltering=defaults.spriteFiltering;next.vignette=defaults.vignette;next.crt=defaults.crt;next.crtRetro=defaults.crtRetro;next.crtBright=defaults.crtBright;next.fxaa=defaults.fxaa;next.smaa=defaults.smaa;next.bloom=defaults.bloom;next.sharpen=defaults.sharpen;}
    } else if(a.optionsPage==14){
        if(row==7){optionsBack(a);return;}
#if VYRUZ_GLES
        if(row<=4){a.menu.error="Post-processing is unavailable in this build";return;}
#endif
        if(row==0){next.fxaa=!next.fxaa;if(next.fxaa)next.smaa=false;}
        else if(row==1){next.smaa=!next.smaa;if(next.smaa)next.fxaa=false;}
        else if(row==2)next.bloom=!next.bloom;
        else if(row==3)next.sharpen=!next.sharpen;
        else if(row==4){const int mode=((next.crt?(next.crtBright?3:next.crtRetro?2:1):0)+step+4)%4;next.crt=mode!=0;next.crtRetro=mode>=2;next.crtBright=mode==3;}
        else if(row==5)next.vignette=!next.vignette;
        else {next.fxaa=defaults.fxaa;next.smaa=defaults.smaa;next.bloom=defaults.bloom;next.sharpen=defaults.sharpen;next.crt=defaults.crt;next.crtRetro=defaults.crtRetro;next.crtBright=defaults.crtBright;next.vignette=defaults.vignette;}
    } else if(a.optionsPage==13){
        if(row==4){optionsBack(a);return;}
        next.windowMode=row;
    } else if(a.optionsPage==2) {
        if(row==7){optionsBack(a);return;}
        if(row<3){a.optionsPage=10+row;a.menu.selected=0;refreshOptions(a);return;}
        if(row==6){a.optionsPage=9;a.menu.selected=0;refreshOptions(a);return;}
        if(row==3)next.audioBits=nextOption(next.audioBits,{8,16,32},step);
        if(row==4)next.audioRate=nextOption(next.audioRate,{11025,22050,48000},step);
        if(row==5)next.audioChannels=3-next.audioChannels;
    } else if(a.optionsPage==9){
        if(row==2){optionsBack(a);return;}
        if(row==0)next.menuSounds=!next.menuSounds;
        else {next.soundFx=next.voices=next.music=next.menuSounds=true;next.effectsVolume=next.voicesVolume=next.musicVolume=1;next.audioBits=32;next.audioRate=48000;next.audioChannels=2;}
    } else if(a.optionsPage>=10&&a.optionsPage<=12){
        if(row==2){optionsBack(a);return;}
        bool& enabled=a.optionsPage==10?next.soundFx:a.optionsPage==11?next.voices:next.music;
        float& volume=a.optionsPage==10?next.effectsVolume:a.optionsPage==11?next.voicesVolume:next.musicVolume;
        if(row==0)enabled=!enabled;else volume=std::clamp(std::round(volume*10+step)/10.f,0.f,1.f);
    } else if(a.optionsPage==3) {
        if(row==7){optionsBack(a);return;}
        if(row==4){next.aimAssist=(next.aimAssist+step+5)%5;}
        else if(row==5)next.crosshair=!next.crosshair;
        else if(row<3){a.optionsPage=row==0?5:row==1?7:6;a.menu.selected=0;refreshOptions(a);return;}
        else if(row==3){a.optionsPage=15;a.menu.selected=0;refreshOptions(a);return;}
        else {
            next.keyBindings=defaults.keyBindings;next.controllerBindings=defaults.controllerBindings;next.fireButton=defaults.fireButton;next.thrustButton=defaults.thrustButton;
            next.joystickMoveSensitivity=defaults.joystickMoveSensitivity;next.joystickAimSensitivity=defaults.joystickAimSensitivity;next.joystickDeadzone=defaults.joystickDeadzone;next.aimAssist=defaults.aimAssist;next.crosshair=defaults.crosshair;
        }
    } else if(a.optionsPage==15) {
        if(row==3){optionsBack(a);return;}
        if(row==0)next.joystickMoveSensitivity=std::clamp(next.joystickMoveSensitivity+step*5,10,100);
        else if(row==1)next.joystickAimSensitivity=std::clamp(next.joystickAimSensitivity+step*5,10,100);
        else if(row==2)next.joystickDeadzone=std::clamp(next.joystickDeadzone+step*2,0,50);
    } else if(a.optionsPage==4) {
        if(row==3){optionsBack(a);return;}
        // Port editor adapter: match the proven 19-byte score name field.
        // Existing longer configuration names remain readable until edited.
        if(row==0){a.bindingCapture=5;a.nameEdit=next.playerName.substr(0,19);a.nameEditBlinkStart=SDL_GetTicks64();a.menu.optionLabels[0]="Player: "+a.nameEdit;SDL_StartTextInput();a.menu.error="Edit player name (19 characters)";a.menu.optionHelp[0]="Type, or use stick/D-pad to edit. Enter/A saves; Esc/B cancels.";return;}
        if(row==1)next.difficulty=(next.difficulty+step+4)%4;
        else next.hudLanguage=3-next.hudLanguage;
    } else if(a.optionsPage==5||a.optionsPage==6) {
        if(row==int(vyruz::optionActionCount)){optionsBack(a);return;}
        a.bindingCapture=a.optionsPage==5?1:2;a.menu.error="Press a button; Esc cancels; Del clears";return;
    } else if(a.optionsPage==7) {
        if(row==2){optionsBack(a);return;}
        a.bindingCapture=row==0?3:4;a.menu.error="Click a button; Esc cancels; Del clears";return;
    }
    const bool saved=commitOptions(a,next);
    if(saved&&a.optionsPage==13){a.optionsPage=8;a.menu.selected=0;}
    refreshOptions(a);
    if(saved&&a.optionsPage==4&&row==1)a.menu.error="Difficulty applies to new games";
}
bool repeatableOption(const App& a){
    const int row=a.menu.selected;
    return (a.optionsPage==1&&row==1)||(a.optionsPage>=10&&a.optionsPage<=12&&row==1)||
        (a.optionsPage==15&&row>=0&&row<=2);
}
void serviceOptionsHold(App& a,uint64_t now){
    if(!a.inputFocused||a.menu.page!=vyruz::MenuPage::Options||a.bindingCapture||!repeatableOption(a)){a.menuHold.reset();return;}
    a.menuHold.context(a.optionsPage*100+a.menu.selected);
    if(const int step=a.menuHold.repeat(now))changeOption(a,step);
}
bool optionsEvent(App& a,SDL_Event event) {
    const bool controllerEvent=event.type==SDL_CONTROLLERBUTTONDOWN||event.type==SDL_CONTROLLERBUTTONUP;
    if(event.type==SDL_KEYUP && (event.key.keysym.sym==SDLK_LEFT||event.key.keysym.sym==SDLK_RIGHT))
        a.menuHold.release(false,event.key.keysym.sym==SDLK_LEFT?-1:1);
    if(event.type==SDL_CONTROLLERBUTTONUP&&a.controller.owns(event.cbutton.which)){
        if(event.cbutton.button==SDL_CONTROLLER_BUTTON_DPAD_LEFT)a.menuHold.release(true,-1);
        if(event.cbutton.button==SDL_CONTROLLER_BUTTON_DPAD_RIGHT)a.menuHold.release(true,1);
    }
    if(event.type==SDL_QUIT||event.type==SDL_WINDOWEVENT||event.type==SDL_CONTROLLERDEVICEADDED||event.type==SDL_CONTROLLERDEVICEREMOVED)return false;
    a.suppressFireUntilRelease=true;
    if(a.bindingCapture) {
        if((event.type==SDL_KEYDOWN && event.key.keysym.sym==SDLK_ESCAPE) ||
           (event.type==SDL_CONTROLLERBUTTONDOWN&&a.controller.owns(event.cbutton.which)&&
            (event.cbutton.button==SDL_CONTROLLER_BUTTON_B||event.cbutton.button==SDL_CONTROLLER_BUTTON_START))){optionsBack(a);return true;}
        if((a.bindingCapture==1||a.bindingCapture==2)&&event.type==SDL_MOUSEBUTTONDOWN&&event.button.button==SDL_BUTTON_RIGHT){optionsBack(a);return true;}
        auto next=a.options;bool accept=false;
        if(a.bindingCapture==5) {
            const auto previousName=a.nameEdit;
            if(event.type==SDL_CONTROLLERBUTTONDOWN)a.menu.optionHelp[0]="Up/Down: letter\nRight: add; Left: erase\nA: save; B: cancel";
            if(event.type==SDL_MOUSEWHEEL||event.type==SDL_MOUSEBUTTONDOWN)a.menu.optionHelp[0]="Wheel: letter\nMiddle: add; Right: erase\nLeft: save";
            if(event.type==SDL_TEXTINPUT)for(unsigned char c:std::string(event.text.text))if(c>=32&&c<127&&a.nameEdit.size()<19)a.nameEdit+=char(c);
            if(event.type==SDL_KEYDOWN && event.key.keysym.sym==SDLK_BACKSPACE && !a.nameEdit.empty())a.nameEdit.pop_back();
            if(event.type==SDL_KEYDOWN && !event.key.repeat && (event.key.keysym.sym==SDLK_RETURN||event.key.keysym.sym==SDLK_KP_ENTER)){next.playerName=a.nameEdit;accept=true;}
            int letter=0;bool add=false,erase=false;
            if(event.type==SDL_KEYDOWN){
                letter=event.key.keysym.sym==SDLK_UP?1:event.key.keysym.sym==SDLK_DOWN?-1:0;
                add=event.key.keysym.sym==SDLK_RIGHT;erase=event.key.keysym.sym==SDLK_LEFT;
            }
            if(event.type==SDL_CONTROLLERBUTTONDOWN&&a.controller.owns(event.cbutton.which)){
                letter=event.cbutton.button==SDL_CONTROLLER_BUTTON_DPAD_UP?1:event.cbutton.button==SDL_CONTROLLER_BUTTON_DPAD_DOWN?-1:0;
                add=event.cbutton.button==SDL_CONTROLLER_BUTTON_DPAD_RIGHT;
                erase=event.cbutton.button==SDL_CONTROLLER_BUTTON_DPAD_LEFT;
                if(event.cbutton.button==SDL_CONTROLLER_BUTTON_A){next.playerName=a.nameEdit;accept=true;}
            }
            if(event.type==SDL_MOUSEWHEEL)letter=wheelSteps(event.wheel);
            if(event.type==SDL_MOUSEBUTTONDOWN){
                add=event.button.button==SDL_BUTTON_MIDDLE;erase=event.button.button==SDL_BUTTON_RIGHT;
                if(event.button.button==SDL_BUTTON_LEFT){next.playerName=a.nameEdit;accept=true;}
            }
            if(erase&&!a.nameEdit.empty())a.nameEdit.pop_back();
            if(add&&a.nameEdit.size()<19)a.nameEdit+=' ';
            if(letter){
                const std::string alphabet=" ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_.";
                if(a.nameEdit.empty())a.nameEdit+=' ';
                const auto found=alphabet.find(a.nameEdit.back());
                const int index=found==std::string::npos?0:int(found),n=int(alphabet.size());
                a.nameEdit.back()=alphabet[((index+letter)%n+n)%n];
            }
            if(a.nameEdit!=previousName)a.nameEditBlinkStart=SDL_GetTicks64();
            a.menu.optionLabels[0]="Player: "+a.nameEdit;
        } else if(event.type==SDL_KEYDOWN && event.key.keysym.sym==SDLK_DELETE) {
            if(a.bindingCapture==1)next.keyBindings[a.menu.selected]=0;
            if(a.bindingCapture==2)next.controllerBindings[a.menu.selected]=-1;
            if(a.bindingCapture==3)next.fireButton=0;
            if(a.bindingCapture==4)next.thrustButton=0;
            accept=true;
        } else if(a.bindingCapture==1 && event.type==SDL_KEYDOWN&&!event.key.repeat) {
            const int key=event.key.keysym.scancode;
            if(a.debugEnabled&&key==SDL_SCANCODE_F2){a.menu.error="F2 opens Debug while --debug is enabled";return true;}
            // Reassigning an occupied binding swaps it, so actions remain reachable.
            auto& list=next.keyBindings;auto found=std::find(list.begin(),list.end(),key);
            if(found!=list.end())*found=list[a.menu.selected];
            list[a.menu.selected]=key;accept=true;
        } else if(a.bindingCapture==2 && event.type==SDL_CONTROLLERBUTTONDOWN&&a.controller.owns(event.cbutton.which)) {
            auto& list=next.controllerBindings;auto found=std::find(list.begin(),list.end(),event.cbutton.button);
            if(found!=list.end())*found=list[a.menu.selected];
            list[a.menu.selected]=event.cbutton.button;accept=true;
        } else if((a.bindingCapture==3||a.bindingCapture==4)&&event.type==SDL_MOUSEBUTTONDOWN) {
            const int old=a.bindingCapture==3?next.fireButton:next.thrustButton;
            if(a.bindingCapture==3){if(next.thrustButton==event.button.button)next.thrustButton=old;next.fireButton=event.button.button;}
            else {if(next.fireButton==event.button.button)next.fireButton=old;next.thrustButton=event.button.button;}accept=true;
        }
        if(accept && commitOptions(a,next)){a.bindingCapture=0;SDL_StopTextInput();refreshOptions(a);}return true;
    }
    if(event.type==SDL_CONTROLLERBUTTONDOWN&&a.controller.owns(event.cbutton.which)) {
        SDL_Keycode key=0;
        switch(event.cbutton.button){case SDL_CONTROLLER_BUTTON_DPAD_UP:key=SDLK_UP;break;case SDL_CONTROLLER_BUTTON_DPAD_DOWN:key=SDLK_DOWN;break;
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT:key=SDLK_LEFT;break;case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:key=SDLK_RIGHT;break;
        case SDL_CONTROLLER_BUTTON_A:key=SDLK_RETURN;break;case SDL_CONTROLLER_BUTTON_B:case SDL_CONTROLLER_BUTTON_START:key=SDLK_ESCAPE;break;}
        event={};event.type=SDL_KEYDOWN;event.key.keysym.sym=key;
    }
    if(event.type==SDL_KEYDOWN&&!event.key.repeat) {
        if((event.key.keysym.sym==SDLK_LEFT||event.key.keysym.sym==SDLK_RIGHT)&&repeatableOption(a)){
            a.menuHold.context(a.optionsPage*100+a.menu.selected);
            a.menuHold.press(controllerEvent,event.key.keysym.sym==SDLK_LEFT?-1:1,SDL_GetTicks64());
        } else a.menuHold.reset();
        switch(event.key.keysym.sym){case SDLK_BACKSPACE:case SDLK_ESCAPE:optionsBack(a);break;case SDLK_UP:a.menu.move(-1);break;case SDLK_DOWN:a.menu.move(1);break;
        case SDLK_LEFT:changeOption(a,-1);break;case SDLK_RIGHT:case SDLK_SPACE:case SDLK_KP_ENTER:case SDLK_RETURN:changeOption(a);break;default:break;}
    } else if(event.type==SDL_MOUSEMOTION||event.type==SDL_MOUSEBUTTONDOWN) {
        const auto v=a.gfx.viewport();const auto point=a.gfx.mousePoint(event.type==SDL_MOUSEMOTION?event.motion.x:event.button.x,event.type==SDL_MOUSEMOTION?event.motion.y:event.button.y);
        const float x=point.x-v.uiX(),y=point.y-v.uiY();
        if(a.menu.hover(x,y)&&event.type==SDL_MOUSEBUTTONDOWN){
            if(event.button.button==SDL_BUTTON_LEFT)changeOption(a);
            if(event.button.button==SDL_BUTTON_RIGHT)changeOption(a,-1);
        }
    } else if(event.type==SDL_MOUSEWHEEL)a.menu.move(-wheelSteps(event.wheel));
    return true;
}
