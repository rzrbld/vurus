// Original Extras resources and modal navigation. Included after the level loader.
bool loadExtraTexture(App& a,const std::string& path,bool gaf) {
    std::vector<uint8_t> bytes;vyruz::Image image;GLuint texture=0;
    if(!a.vfs.read(path,bytes))return false;
    if(gaf) {
        vyruz::GafAnimation bank;
        if(!vyruz::decodeGafAnimation(bytes,bank,vyruz::GafDecodeMode::FirstSequencePose)||bank.sequences.empty()||bank.sequences[0].subframes.empty())return false;
        const auto frame=bank.sequences[0].subframes[0].frameIndex;
        if(frame>=bank.frames.size())return false;
        const auto& bitmap=bank.frames[frame].image;
        image.width=bitmap.width;image.height=bitmap.height;texture=a.gfx.uploadTexture(bitmap);
    } else if(!vyruz::loadTga(bytes,image))return false;
    if(!gaf)texture=a.gfx.uploadTexture(image.rgba.data(),image.width,image.height);
    if(!texture)return false;
    a.gfx.deleteTexture(a.extraTex);a.extraTex=texture;a.extraWidth=image.width;a.extraHeight=image.height;return true;
}
bool nextSlide(App& a) {
    for(unsigned attempt=0;attempt<31;++attempt) {
        a.slideIndex=a.slideIndex%31+1;
        char path[64];std::snprintf(path,sizeof(path),"slides/slide-%03u.tga",a.slideIndex);
        if(loadExtraTexture(a,path,false))return true;
    }
    return false;
}
void loadScores(App& a) {
    if(a.scoresLoaded)return;
    a.scores=vyruz::defaultHighScores(0xabcdefda);a.scoresLoaded=true;
    if(a.scorePath.empty())a.scorePath=(std::filesystem::path(defaultSavePath()).parent_path()/"vz.sco").string();
    std::string error;std::error_code ec;
    if(std::filesystem::exists(a.scorePath,ec)) {
        if(!vyruz::readHighScores(a.scorePath,a.scores,error))std::fprintf(stderr,"scores: %s\n",error.c_str());
    } else {
        std::vector<uint8_t> bytes;
        if(a.vfs.read("vz.sco",bytes)&&!vyruz::decodeHighScores(bytes,a.scores,error))std::fprintf(stderr,"original scores: %s\n",error.c_str());
    }
}
void recordScore(App& a) {
    if(a.scoreRecorded||a.demoActive||!a.sessionStarted)return;
    a.scoreRecorded=true;loadScores(a);
    unsigned tower=0,level=0;if(std::sscanf(a.content.levelId.c_str(),"%u.%u",&tower,&level)!=2)return;
    const auto carry=a.world.carry();
    const auto entry=vyruz::makeHighScore(a.options.playerName,tower,level,
        static_cast<uint32_t>(std::min<uint64_t>(carry.kills,UINT32_MAX)),
        static_cast<uint32_t>(std::clamp<int64_t>(carry.vitals.lifetimeMoney,0,UINT32_MAX)),
        static_cast<uint64_t>(std::max(0.0,carry.elapsedMs)),
        static_cast<uint32_t>(std::min<uint64_t>(carry.ammoHits,UINT32_MAX)),
        static_cast<uint32_t>(std::min<uint64_t>(carry.ammoMisses,UINT32_MAX)));
    auto next=a.scores;if(!vyruz::insertHighScore(next,entry))return;
    std::error_code ec;const auto directory=std::filesystem::path(a.scorePath).parent_path();
    if(!directory.empty())std::filesystem::create_directories(directory,ec);
    std::string error;
    if(ec||!vyruz::writeHighScores(a.scorePath,next,error))std::fprintf(stderr,"score save failed: %s\n",ec?ec.message().c_str():error.c_str());
    else a.scores=std::move(next);
}
bool beginExtra(App& a,vyruz::MenuAction action) {
    using P=vyruz::PresentationPage;using A=vyruz::MenuAction;
    if(action==A::ControlsHelp) {
        a.helpRows.clear();a.helpFirst=0;a.helpNextScroll=0;
        const auto add=[&](std::string label,std::string binding,int slot=-1){a.helpRows.push_back({std::move(label)+" : "+std::move(binding),slot});};
        add("Information","F1");if(a.debugEnabled)add("Debug menu","F2");
        for(int i=0;i<9;++i)add("Weapon #"+std::to_string(i+1),std::to_string(i+1),i);
        for(auto action:{vyruz::OptionAction::Fire,vyruz::OptionAction::NextWeapon,vyruz::OptionAction::PrevWeapon,vyruz::OptionAction::Inventory,vyruz::OptionAction::Map,vyruz::OptionAction::Pause,vyruz::OptionAction::PanelHelp,vyruz::OptionAction::SpeedMode,vyruz::OptionAction::Forward,vyruz::OptionAction::Reverse,vyruz::OptionAction::Left,vyruz::OptionAction::Right}) {
            const auto index=static_cast<size_t>(action);std::string binding;
            if(a.options.keyBindings[index]>0)binding=SDL_GetScancodeName(static_cast<SDL_Scancode>(a.options.keyBindings[index]));
            if(a.options.controllerBindings[index]>=0){const char* button=SDL_GameControllerGetStringForButton(static_cast<SDL_GameControllerButton>(a.options.controllerBindings[index]));if(button){if(!binding.empty())binding+=" / ";binding+=button;}}
            if(!binding.empty())add(vyruz::optionActionName(action),binding);
        }
        add("Menu","Esc / Start");add("Aim","Mouse / right stick");add("Move","Left stick / arrows");
        if(a.options.fireButton)add("Mouse fire","Button "+std::to_string(a.options.fireButton));
        if(a.options.thrustButton)add("Mouse forward","Button "+std::to_string(a.options.thrustButton));
        add("Controller fire","Right trigger");
        beginPresentation(a,P::ControlsHelp);
        //43b67a: opening controls help queues HELP after presentation reset.
        a.audio.speech("V-HELP.WAV",0,1000,5000);
        a.audio.updateSpeech(); // Controls presentation does not run gameplay speech updates.
    } else if(action==A::Intro) {
        if(!beginMovie(a)){a.menu.error="Intro animation file is not present or cannot be decoded.";return false;}
    } else if(action==A::Credits) {
        const auto text=presentationText(a,a.audioLanguage=="FRENCH"?"COMMON/DATA/EXTRA/CREDITS-FR.TXT":"COMMON/DATA/EXTRA/CREDITS-EN.TXT");
        if(text.empty()||!loadExtraTexture(a,"COMMON/DATA/EXTRA/CREDITS.GAF",true)){a.menu.error="Original credits resources are missing.";return false;}
        beginPresentation(a,P::Credits,text);
    } else if(action==A::Scores) {
        if(!loadExtraTexture(a,"COMMON/DATA/EXTRA/SCORE.GAF",true)){a.menu.error="Original score background is missing.";return false;}
        loadScores(a);beginPresentation(a,P::Scores);
    } else if(action==A::SlideShow) {
        a.slideIndex=0;if(!nextSlide(a)){a.menu.error="Original slideshow resources are missing.";return false;}
        a.slideDeadline=4000;beginPresentation(a,P::SlideShow);
    } else return false;
    a.presentationReturnExtras=true;return true;
}
void drawExtras(App& a) {
    const auto view=a.gfx.viewport();a.gfx.clear(0,0,0);
    const float x=(view.width-a.extraWidth)/2,y=(view.height-a.extraHeight)/2;
    glDisable(GL_BLEND);glColor4f(1,1,1,1);a.gfx.beginQuads(a.extraTex);
    a.gfx.quad(x,y,0,0,x+a.extraWidth,y,1,0,x+a.extraWidth,y+a.extraHeight,1,1,x,y+a.extraHeight,0,1);a.gfx.endQuads();glEnable(GL_BLEND);
    glPushMatrix();glTranslatef(view.uiX(),view.uiY(),0);
    if(a.presentation.page==vyruz::PresentationPage::Scores)a.hud.drawHighScores(a.gfx,a.scores);
    if(a.presentation.page==vyruz::PresentationPage::Credits) {
        // 402400 moves .5px per native update; use the accepted 14ms UI cadence.
        const float top=479-float(a.presentation.elapsedMilliseconds()/14*.5);
        GLint viewport[4];glGetIntegerv(GL_VIEWPORT,viewport);
        glEnable(GL_SCISSOR_TEST);
        glScissor(viewport[0]+int((view.uiX()+380)*viewport[2]/view.width),viewport[1]+int(view.uiY()*viewport[3]/view.height),
                  int(260*viewport[2]/view.width),int(480*viewport[3]/view.height));
        const float height=a.hud.drawMenuText(a.gfx,a.presentation.visibleText(),380,top,4,260,true);
        glDisable(GL_SCISSOR_TEST);
        if(top+height<0)finishPresentation(a);
    }
    glPopMatrix();a.gfx.present();
}

// 43e5f0 -> 445a40 / 404950 (5s or input) -> 43e470(insert=1).
void finishGameOver(App& a) {
    recordScore(a);
    const bool scores=beginExtra(a,vyruz::MenuAction::Scores);
    a.presentationReturnExtras=false;a.sessionStarted=false;
    if(scores)a.presentationReturnMain=true;
    else {a.menu.show(a.paused,true);a.menu.retryAvailable=a.restartLevel.has_value();}
}
void beginGameOverCard(App& a) {
    if(loadExtraTexture(a,"COMMON/DATA/EXTRA/GAME-OVER.GAF",true)) {
        beginPresentation(a,vyruz::PresentationPage::GameOver);
        a.audio.cue("V-GAME-OVER.WAV",true);
    }
    else finishGameOver(a);
}
void serviceGameOver(App& a) {
    if(a.demoActive || !a.sessionStarted || a.world.player().alive || a.deathPresented || a.presentation.open())return;
    a.deathPresented=true;a.menu.close(a.paused);a.inventory={};a.restart=false;a.menuAction=vyruz::MenuAction::None;
    a.paused=false;
    beginPresentation(a,vyruz::PresentationPage::DeathPrompt);
}
