// Opt-in actual App/GL audit; original resources are supplied locally.
#define main vyruzApplicationMain
#include "../src/main.cpp"
#undef main
#include <stdexcept>
static void need(bool value,const std::string& message){if(!value)throw std::runtime_error(message);}
int main(int argc,char** argv)try {
    need(argc==3||(argc==4&&std::string_view(argv[3])=="--fixed-display"),"usage: pause_redraw_audit DATA OUTPUT [--fixed-display]");
    const bool fixedDisplay=argc==4;
    const std::filesystem::path out=argv[2];std::filesystem::create_directories(out);
    need(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_AUDIO|SDL_INIT_GAMECONTROLLER)==0,SDL_GetError());App a;
#if VYRUZ_GLES
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,3);SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,0);
#endif
    a.window=SDL_CreateWindow("Pause redraw audit",0,0,640,480,SDL_WINDOW_OPENGL|SDL_WINDOW_HIDDEN|SDL_WINDOW_RESIZABLE);
    need(a.window&&a.gfx.init(a.window),SDL_GetError());need(a.vfs.init(argv[1]),a.vfs.error());
    a.retryPersistence=false;a.savePath=(out/"unused.vzs").string();a.optionsPath=(out/"options.cfg").string();a.scorePath=(out/"scores").string();
    need(loadLevelData(a,"CAVE/T1C1.3DG"),a.loadError);a.presentation.close();a.menu.close(a.paused);a.sessionStarted=true;
    a.gfx.setSwapEnabled(false);SDL_FlushEvents(SDL_FIRSTEVENT,SDL_LASTEVENT);
    need(renderApplicationFrame(a)&&renderApplicationFrame(a),"active gameplay always renders");
    const auto key=[&](SDL_Keycode code){SDL_Event e{};e.type=SDL_KEYDOWN;e.key.keysym.sym=code;e.key.keysym.scancode=SDL_GetScancodeFromKey(code);need(SDL_PushEvent(&e)==1,"push key");handleInput(a);};
    const auto windowEvent=[&](Uint8 code){SDL_Event e{};e.type=SDL_WINDOWEVENT;e.window.event=code;need(SDL_PushEvent(&e)==1,"push window event");handleInput(a);};
    key(SDLK_p);need(ordinaryPause(a),"ordinary pause entered through input");
    need(renderApplicationFrame(a),"first pause composition is presented");
    const auto state=[&](bool scene=true){vyruz::SaveGame save;save.currentLevel=a.content.mapPath;save.levels[save.currentLevel].world=a.world.snapshot();if(scene)save.levels[save.currentLevel].render=a.sceneRenderer.capture();std::string error;std::vector<uint8_t> bytes;need(vyruz::encodeSave(save,bytes,error),error);return bytes;};
    const auto pixels=[&]{std::vector<uint8_t> data(size_t(a.gfx.drawableWidth())*a.gfx.drawableHeight()*4);glFinish();glReadPixels(0,0,a.gfx.drawableWidth(),a.gfx.drawableHeight(),GL_RGBA,GL_UNSIGNED_BYTE,data.data());return data;};
    const auto frozen=state(),frozenWorld=state(false),image=pixels();
    const auto frozenRender=a.sceneRenderer.capture();
    for(unsigned i=0;i<180;++i){handleInput(a);need(!renderApplicationFrame(a),"unchanged pause skips composition/present");}
    need(state()==frozen,"idle pause preserves full World and scene clocks");need(pixels()==image,"idle pause retains displayed composition");
    need(saveScreenshot((out/"pause-640.ppm").c_str(),640,480),"pause capture");
    windowEvent(SDL_WINDOWEVENT_EXPOSED);need(renderApplicationFrame(a)&&!renderApplicationFrame(a),"exposure redraws exactly once");
    key(SDLK_F1);need(renderApplicationFrame(a)&&!renderApplicationFrame(a),"information toggle redraws once");
    const int resizedW=fixedDisplay?640:1280,resizedH=fixedDisplay?480:720;
    SDL_SetWindowSize(a.window,resizedW,resizedH);windowEvent(SDL_WINDOWEVENT_SIZE_CHANGED);
    a.gfx.setSwapEnabled(true);need(renderApplicationFrame(a),"resize swaps to refresh double-buffer storage");
    a.gfx.setSwapEnabled(false);need(renderApplicationFrame(a)&&!renderApplicationFrame(a),"resize completes fresh drawable then stops rendering");
    need(a.gfx.drawableWidth()==resizedW&&a.gfx.drawableHeight()==resizedH,"drawable size refreshed");
    need(saveScreenshot((out/(fixedDisplay?"pause-native.ppm":"pause-1280.ppm")).c_str(),resizedW,resizedH),"resized pause capture");
    need(state(false)==frozenWorld,"expose/toggle/resize preserve World state");
    // A wider view admits formerly offscreen bodies and selects their pose
    // banks (Cave objects895/1028:1->25). That is not elapsed animation time.
    const auto resized=a.sceneRenderer.capture();need(resized.channels.size()==frozenRender.channels.size(),"resize preserves channel identities");
    for(size_t i=0;i<resized.channels.size();++i){const auto& b=frozenRender.channels[i].cursor;const auto& c=resized.channels[i].cursor;need(b.cursor==c.cursor&&b.flags==c.flags&&b.remaining==c.remaining,"resize preserves cursor clocks");}
    const auto resizedState=state();need(!renderApplicationFrame(a)&&state()==resizedState,"resized pause remains fully frozen when idle");
    windowEvent(SDL_WINDOWEVENT_FOCUS_LOST);need(a.menu.open()&&!ordinaryPause(a),"focus loss opens responsive menu");
    windowEvent(SDL_WINDOWEVENT_FOCUS_GAINED);
    need(renderApplicationFrame(a)&&renderApplicationFrame(a),"menu keeps rendering");
    a.menu.close(a.paused);need(ordinaryPause(a)&&renderApplicationFrame(a)&&!renderApplicationFrame(a),"return to paused game refreshes once");
    a.inventory.toggle(a.paused);need(!ordinaryPause(a)&&renderApplicationFrame(a)&&renderApplicationFrame(a),"inventory remains live");a.inventory.close(a.paused);
    a.presentation.show(vyruz::PresentationPage::Mission,"Mission text");need(!ordinaryPause(a),"modal presentation is excluded");
    need(renderApplicationFrame(a)&&renderApplicationFrame(a),"mission presentation keeps rendering");a.presentation.close();
    a.presentation.show(vyruz::PresentationPage::DeathPrompt,"");need(!ordinaryPause(a),"live death prompt is excluded");a.presentation.close();
    a.menuExit=true;need(!ordinaryPause(a),"exit effect is excluded");a.menuExit=false;
#if VYRUZ_GLES
    a.savePreviewWritePath=(out/"preview.tga").string();need(!ordinaryPause(a),"pending GLES preview prevents idle suppression");a.savePreviewWritePath.clear();
#endif
    need(renderApplicationFrame(a)&&!renderApplicationFrame(a),"pause returns after modal presentation");
    need(renderApplicationFrame(a,1,true),"explicit screenshot forces a composition");
    SDL_FlushEvents(SDL_FIRSTEVENT,SDL_LASTEVENT);SDL_Event first{},second{};first.type=second.type=SDL_USEREVENT;first.user.code=71;second.user.code=72;
    need(SDL_PushEvent(&first)==1&&SDL_PushEvent(&second)==1,"push ordered wake events");
    need(SDL_WaitEventTimeout(nullptr,16)==1,"queued event wakes idle wait");
    SDL_Event event{};need(SDL_PollEvent(&event)&&event.type==SDL_USEREVENT&&event.user.code==71,"wait preserves first event");
    need(SDL_PollEvent(&event)&&event.type==SDL_USEREVENT&&event.user.code==72,"wait preserves second event");
    SDL_FlushEvents(SDL_FIRSTEVENT,SDL_LASTEVENT); // End the manual polling batch before the next synthetic key.
    key(SDLK_p);need(!a.paused&&renderApplicationFrame(a)&&renderApplicationFrame(a),"resume input restores continuous rendering");
    std::ofstream(out/"result.txt")<<"PASS 180 skipped pause redraws, full World/scene freeze, retained pixels, expose/resize, focus/menu/inventory/modal exclusions, forced capture, ordered wake events and resume.\n";
    a.audio.device().close();a.loading.clear(a.gfx);a.sceneRenderer.clear(a.gfx);a.hud.clear(a.gfx);a.gfx.deleteTexture(a.atlasTex);a.gfx.shutdown();SDL_DestroyWindow(a.window);SDL_Quit();return 0;
}catch(const std::exception& e){std::fprintf(stderr,"pause audit: %s\n",e.what());return 1;}
