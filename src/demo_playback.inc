// Included by main.cpp after loadLevelData, inside its anonymous namespace.
// App owns vyruz::DemoReplay demo and bool demoActive=false.
void stopDemo(App& a) {
    a.demoActive=false;a.demo.reset();
    a.sessionStarted=false;a.restart=false;a.restartLevel.reset();
    a.inventory={};a.menuAction=vyruz::MenuAction::None;
    a.presentation.close();a.audio.reset();a.audio.device().stopEffects();
    a.retryCheckedProfile.clear();recoverRetrySession(a);
    a.menu.show(a.paused,true);a.menu.retryAvailable=a.restartLevel.has_value();a.suppressFireUntilRelease=true;
    a.previousPositions.clear();
}
bool startDemo(App& a) {
    std::vector<uint8_t> bytes;std::string error;vyruz::DemoReplay replay;
    if(!a.vfs.read("CAVE/DEMO.3DR",bytes) || !replay.load(bytes,error)) {
        a.loadError=error.empty()?"Original CAVE/DEMO.3DR is missing":error;return false;
    }
    // A fresh ordinary resource world; no debug ammo, invulnerability or AI changes.
    if(!loadLevelData(a,"CAVE/DEMO.3DG",nullptr,false,false,false))return false;
    a.demo=std::move(replay);a.demoActive=true;a.sessionStarted=true;
    a.hud.beginDemoOverlay();
    a.restart=false;a.restartLevel.reset();
    a.menu.close(a.paused);a.inventory={};a.suppressFireUntilRelease=true;
    const auto initial=a.demo.advance(0);
    if(initial.pose) {
        a.world.setPlayerPosition(initial.pose->x,initial.pose->y);
        a.world.setPlayerDirection(initial.pose->direction);
        a.worldX=initial.pose->x;a.worldY=initial.pose->y;
        a.motion.direction=initial.pose->direction;
    }
    if(initial.weaponSlot)a.world.selectPlayerWeaponSlot(*initial.weaponSlot);
    a.previousPositions.clear();return true;
}
bool serviceAttractDemo(App& a,uint64_t now) {
    if(!a.presentation.open()&&a.menu.page==vyruz::MenuPage::Main&&!a.menu.retryAvailable&&!a.demoActive) {
        if(!a.mainMenuIdleSince)a.mainMenuIdleSince=now;
        if(now-a.mainMenuIdleSince>=30000){a.mainMenuIdleSince=now;return startDemo(a);}
    } else a.mainMenuIdleSince=0;
    return false;
}
void advanceDemo(App& a,double milliseconds) {
    if(!a.demoActive)return;
    a.hud.advanceDemoOverlay(milliseconds);
    // Native playback uses the narrow436690 margin and the previous camera.
    // Capture before applying this frame's authoritative recorded pose.
    bindEffectCamera(a);
    const auto frame=a.demo.advance(milliseconds);
    if(frame.ended || !a.world.player().alive) { stopDemo(a);return; }
    if(frame.weaponSlot)a.world.selectPlayerWeaponSlot(*frame.weaponSlot);
    //438800 admits recorded pose and digital commands inside the player visit.
    //The ordinary solver/contact/Surprise path then runs; no post-tick reset.
    vyruz::WorldInput input;
    input.fire=frame.fire();input.replayPose=frame.pose;
    const auto flags=frame.flags;
    if(input.replayPose)input.replayPose->flags=flags;
    input.movement.forward=flags&4?1:flags&8?-1:0;
    input.movement.strafe=flags&0x80?1:flags&0x40?-1:0;
    a.world.tick(static_cast<float>(milliseconds/1000),input);
    const auto position=a.world.playerPosition();
    a.worldX=position.x;a.worldY=position.y;a.motion.direction=a.world.player().direction;
    if(a.world.transition() || !a.world.player().alive)stopDemo(a);
}
