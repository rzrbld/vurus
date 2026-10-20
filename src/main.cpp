#include <limits>
#include "video/window_mode.h"
#include <SDL.h>

#include <algorithm>
#include <cmath>
#include <chrono>
#include <thread>
#include <future>
#include <mutex>
#include <cstdio>
#include <cstdlib>
#if defined(__GLIBC__)
#include <malloc.h>
#endif
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include "io/portable_storage.h"
#include "io/process_memory.h"

#include "assets/gaf.h"
#include "audio/game_audio.h"
#include "assets/gaf_sprite.h"
#include "assets/tga.h"
#include "assets/intro_video.h"
#include "video/licenses.h"
#include "crt_license.h"
#include "smaa_license.h"
#include "game/controls.h"
#include "game/options.h"
#include "game/video_modes.h"
#include "game/tile_body_resources.h"
#include "game/controller.h"
#include "game/aim_assist.h"
#include "game/menu_hold.h"
#include "game/menu_stick.h"
#include "game/content.h"
#include "game/high_scores.h"
#include "game/demo_replay.h"
#include "game/save_file.h"
#include "game/save_catalog.h"
#include "game/level.h"
#include "game/lift_access.h"
#include "io/vfs.h"
#include "io/disc_import.h"
#include "render/gfx.h"
#include "render/tile_atlas.h"
#include "render/presentation.h"
#include "render/occlusion.h"
#include "render/scene.h"
#include "render/hud.h"
#include "render/loading_screen.h"
#include "render/first_run_screen.h"

// Vyruz native renderer.
//
// The level is a flat tile surface (see docs/formats/render.md): cell (tx,ty)
// top-left is at surface pixel (tx*64, ty*32); world (x,y) maps to surface
// pixel (x,y). There is no world->screen rotation. The camera is a 2D scroll.
// Tiles are drawn in layer order (painter's algorithm), row-major.

namespace {

void releaseUnusedPortMemory() {
#if defined(__GLIBC__)
    // Large level/snapshot temporaries leave free pages inside glibc arenas.
    // Return unused pages on desktop and handheld before large load/save allocations.
    // Keep this outside per-frame simulation and rendering.
    malloc_trim(0);
#endif
}

constexpr int kVirtualW = 640;
constexpr int kVirtualH = 480;
constexpr int kTileW = 64;
constexpr int kTileH = 32;
constexpr int kAtlasPerRow = 32;
constexpr int kPlayerDirections = 24;          // binary: Vyruz binds ANM indices 1..24

constexpr float kMoveDt = 1.f / 60.f;

struct SaveResult { bool ok=false,retry=false;std::string path,error; };
struct SaveCatalogResult {
    uint64_t generation=0;
    std::string base,profile,error;
    std::vector<vyruz::SaveCatalogEntry> entries;
    std::vector<std::string> labels,help;
};
struct App {
    bool debugEnabled=false;
    bool profileTick=false;
    double profileWorldMs=0;
    double debugFrameSum=0,debugFramePeak=0;unsigned debugFrameCount=0;
    Uint64 debugMemoryNext=0;
    std::vector<std::string> debugMemoryText;
    std::string debugFrameText="FPS --  FRAME -- MS",fpsText="FPS --";
    std::string rssText="RSS N/A";
    vyruz::DemoReplay demo;
    bool demoActive=false;
    GLuint extraTex=0;int extraWidth=640,extraHeight=480;
    unsigned slideIndex=0;double slideDeadline=4000;
    bool presentationReturnExtras=false,scoresLoaded=false,scoreRecorded=false;
    bool slidesAvailable=false;
    bool deathPresented=false,presentationReturnMain=false;
    uint64_t mainMenuIdleSince=0;
    vyruz::HighScores scores{};std::string scorePath;
    SDL_Window* window = nullptr;
    vyruz::Gfx gfx;

    vyruz::Vfs vfs;
    vyruz::GameAudio audio;
    std::string audioLanguage="ENGLISH";
    bool showInfo = true;
    int quit = 0;

    GLuint storyTex = 0;
    GLuint endingTex = 0;
    GLuint endingFrameTex = 0;
    int endingFlash=-1;
    std::string endingSecondText;
    std::string endingFirstText;
    vyruz::Level level;
    vyruz::SceneContent content;
    vyruz::GameWorld world;
    std::optional<vyruz::SavedLevel> restartLevel;
    std::string restartPath,restartProfile,retryDiskPath,retryCheckedProfile;
    std::string retryTitlePath,retryTitle;
    bool retryPersistence=false; //Only actual interactive campaign entries write defaults.
    std::string savePath,lastSavedPath,loadError;
    bool saveHistory=false,saveCatalogDirty=true;
    // Readers and history publication/pruning serialize on workers. Keep this
    // before both futures so every worker joins before the mutex is destroyed.
    std::mutex saveStorageMutex;
    // The worker only reads restartLevel; replacement actions wait until
    // completion. Declared after those snapshots so its destructor joins first.
    std::future<SaveResult> saveFuture;
#if VYRUZ_GLES
    // Destroy the producer before saveFuture: an interrupted render must release
    // a worker waiting for optional pixels, including during stack unwinding.
    std::unique_ptr<std::promise<std::vector<uint8_t>>> savePreviewPromise;
    std::string savePreviewWritePath;
#endif
    // Only paths/profile and the immutable VFS cross to this worker. Declared
    // after VFS so destruction joins before its resource storage is released.
    std::future<SaveCatalogResult> saveCatalogFuture;
    uint64_t saveCatalogGeneration=0;
    std::string saveCatalogBase,saveCatalogProfile;
    std::vector<vyruz::SaveCatalogEntry> saveCatalog;
    GLuint savePreviewTexture=0;
    std::string savePreviewPath;
    bool restart = false;
    bool paused = false;
    vyruz::InventoryView inventory;
    vyruz::MenuView menu;
    bool menuPointerVisible=false;
    vyruz::GameOptions options;
    std::string optionsPath;
    int optionsPage=0, bindingCapture=0;
    vyruz::MenuAdjustmentHold menuHold;
    std::string nameEdit;
    Uint64 nameEditBlinkStart=0;
    std::vector<std::pair<int,int>> resolutions;
    vyruz::Presentation presentation;
    vyruz::IntroVideo intro;
    bool introAttempted=false,introLoaded=false,presentationArmed=false;
    std::vector<std::pair<std::string,int>> helpRows;
    int helpFirst=0;
    Uint64 helpNextScroll=0;
    uint64_t presentationVoice=0,presentationLastTick=0;
    GLuint movieTex=0;
    size_t movieFrame=SIZE_MAX;
    vyruz::MenuAction menuAction=vyruz::MenuAction::None;
    bool sessionStarted=true,suppressFireUntilRelease=false;
    bool controllerBlockedUntilNeutral=false;
    bool inputFocused=true;
    vyruz::HudRenderer hud;
    vyruz::LoadingScreen loading;
    bool loadingActive=false;
    float loadingProgress=0,loadingStageEnd=0;
    int loadingPixel=-1;
    bool renderFailed = false;
    GLuint atlasTex = 0;
    int atlasW = 1, atlasH = 1;
    bool haveLevel = false;

    vyruz::SceneRenderer sceneRenderer;
    double enemyDrawStepMs=0; // last native slice pending one draw (43f07f–43fc42)
    int forcedDir = -1;      // dev: force a facing direction
    std::optional<SDL_Rect> windowedBounds;
    int forcedStep = -1;     // dev: select a raw bitmap (zero-based)

    // Player world position in surface pixels (== world coordinates).
    float worldX = 0.f, worldY = 0.f;
    float camX = 0.f, camY = 0.f;
    float mouseX = 0.f, mouseY = 0.f;
    float padAimX=0,padAimY=0;
    vyruz::AssistedAim assistedAim;
    size_t aimAssistTarget=SIZE_MAX;
    bool mouseUsed = false;   // true once a mouse-motion event arrives
    vyruz::PlayerMotion motion;
    vyruz::ControllerInput controller;
    vyruz::MenuStick menuStick;
    std::vector<vyruz::ContentPosition> previousPositions;
};

bool loadStoryTexture(App& a) {
    if(a.storyTex)return true;
    std::vector<uint8_t> data;
    if (!a.vfs.read("Common\\Data\\Extra\\story.tga", data)) return false;
    vyruz::Image img;
    if (!vyruz::loadTga(data, img)) return false;
    a.storyTex = a.gfx.uploadTexture(img.rgba.data(), img.width, img.height);
    std::printf("loaded story image (%dx%d)\n", img.width, img.height);
    return true;
}

void bindEffectCamera(App& a) {
    if(!a.haveLevel)return;
    const auto view=a.gfx.viewport();const auto center=a.world.playerPosition();
    vyruz::NativeEffectCamera camera;
    camera.viewWidth=static_cast<int32_t>(view.width);
    camera.viewHeight=static_cast<int32_t>(view.height);
    float x=center.x-view.width*.5f,y=center.y-view.height*.5f;
    if(a.world.surprise().feedbackMs>0) {
        x+=a.world.surprise().shakeOffset.x;y+=a.world.surprise().shakeOffset.y;
    }
    camera.offsetX=static_cast<int32_t>(-x);camera.offsetY=static_cast<int32_t>(-y);
    camera.modeFlags=a.demoActive?0x10000:0;
    // Fixed-step adapter: prior authoritative player/shake and the current
    // logical view, never interpolated render coordinates. Native also admits
    // against a previous camera, but its anchor/clamp/frame clock differs.
    a.world.setEffectCamera(camera);
}

vyruz::EnemyAnimationView enemyAnimationView(App& a,float cameraX,float cameraY) {
    const auto view=a.gfx.viewport();
    vyruz::EnemyAnimationView result;
    result.translationX=static_cast<int32_t>(-cameraX);
    result.translationY=static_cast<int32_t>(-cameraY);
    result.width=static_cast<uint32_t>(view.width);result.height=static_cast<uint32_t>(view.height);
    result.mapW=a.level.mapW;result.mapH=a.level.mapH;result.cells=&a.world.nativeActorCells();
    //447320 ->446780 ->4467de sets world20&100 after level graphics load.
    //440336 clears it on game-loop exit. No per14ms clock prerequisite.
    result.visibilityStep=a.haveLevel;
    return result;
}
void advanceSceneAnimation(App& a,double milliseconds,double worldTimeMs) {
    a.sceneRenderer.setNativeEnemyPhases();
    //Retain the existing non-enemy newborn/FX clock admission. Newly bound
    //enemy channels then receive their separate native visible-cell phase.
    a.hud.syncInventory(a.world,[&](size_t id)->std::optional<vyruz::AnimationCursorState> {
        for(const auto& key:a.world.player().heldKeys)if(key.held && key.held->mapPath==a.content.mapPath && key.held->object.id.value==id)
            return a.sceneRenderer.itemCursor(id,key.held->object.sprite.sequence.value);
        return {};
    });
    a.sceneRenderer.advance(milliseconds,&a.world.actors(),worldTimeMs,&a.world.surprise());
    std::string error;
    if(!a.sceneRenderer.sync(a.content,error)) {
        std::fprintf(stderr,"render update failed: %s\n",error.c_str());a.renderFailed=true;return;
    }
    if(!a.haveLevel||milliseconds<=0)return;
    const double enemyMs=std::floor(worldTimeMs)-std::floor(worldTimeMs-milliseconds);
    a.enemyDrawStepMs=enemyMs;
    const auto viewport=a.gfx.viewport();const auto center=a.world.playerPosition();
    float x=center.x-viewport.width*.5f,y=center.y-viewport.height*.5f;
    if(a.world.surprise().feedbackMs>0){x+=a.world.surprise().shakeOffset.x;y+=a.world.surprise().shakeOffset.y;}
    auto view=enemyAnimationView(a,x,y);
    view.playerOnly=a.world.surprise().active(vyruz::SurpriseKind::TimeWarp)&&a.world.surprise().timeWarpPhase!=1;
    a.sceneRenderer.advanceEnemyPhase(enemyMs,a.world.actors(),view,vyruz::EnemyAnimationPhase::VisibleUpdate);
}

std::string presentationText(App& a,const std::string& path) {
    std::vector<uint8_t> data;
    if(!a.vfs.read(path,data)||data.empty())return {};
    if(data.size()>65536 || std::find(data.begin(),data.end(),0)!=data.end()) {
        std::fprintf(stderr,"invalid presentation text: %s\n",path.c_str());return {};
    }
    return std::string(data.begin(),data.end());
}
void beginPresentation(App& a,vyruz::PresentationPage page,std::string text={}) {
    a.presentation.show(page,std::move(text));a.presentationArmed=false;
    if(page==vyruz::PresentationPage::EndingTerminal) {
        SDL_DisplayMode mode{};const int display=SDL_GetWindowDisplayIndex(a.window);
        a.presentation.setTerminalRefresh(display>=0 && SDL_GetCurrentDisplayMode(display,&mode)==0?mode.refresh_rate:60);
    }
    a.presentationLastTick=SDL_GetTicks64();a.suppressFireUntilRelease=true;
    if(page!=vyruz::PresentationPage::DeathPrompt)a.audio.reset();
    a.audio.device().setMusicPaused(page!=vyruz::PresentationPage::Mission && page!=vyruz::PresentationPage::DeathPrompt);
}
vyruz::AudioClipPtr introAudio(App& a,std::string& error){
    const auto& wave=a.intro.audio();
    if(a.intro.audioChunks().empty())return {};
    auto stream=vyruz::WavStream::openPcm(a.intro.path(),wave.sampleRate,wave.channels,wave.bitsPerSample,a.intro.audioChunks(),error);
    if(!stream)return {};
    auto clip=std::make_shared<vyruz::AudioClip>();clip->wave.sampleRate=wave.sampleRate;
    clip->wave.channels=wave.channels;clip->wave.bitsPerSample=wave.bitsPerSample;
    clip->frames=stream->frames();clip->stream=std::move(stream);return clip;
}
bool beginMovie(App& a) {
    if(!a.introAttempted) {
        a.introAttempted=true;std::string error;
        a.introLoaded=a.intro.loadFile(a.vfs.loosePath("Intro.avi"),error);
        if(!a.introLoaded)std::fprintf(stderr,"intro skipped: %s\n",error.empty()?"Intro.avi is absent; restore it from your original disc image":error.c_str());
    }
    if(!a.introLoaded)return false;
    beginPresentation(a,vyruz::PresentationPage::Movie);
    std::string error;auto audio=introAudio(a,error);
    if(!audio&&!error.empty())std::fprintf(stderr,"intro audio: %s\n",error.c_str());
    a.presentationVoice=audio?a.audio.device().play(audio,255,1,0,false,true,vyruz::AudioBus::Presentation):0;
    a.movieFrame=SIZE_MAX;return true;
}
void finishGameOver(App& a);
void beginGameOverCard(App& a);
void finishPresentation(App& a) {
    const auto page=a.presentation.page;
    a.audio.device().stop(a.presentationVoice);a.presentationVoice=0;
    a.presentation.close();a.suppressFireUntilRelease=true;
    a.gfx.deleteTexture(a.movieTex);a.movieTex=0;a.movieFrame=SIZE_MAX;
    if(page==vyruz::PresentationPage::Movie){
        a.intro={};a.introLoaded=false;a.introAttempted=false;
        releaseUnusedPortMemory(); // Extras can reopen the movie on demand.
    }
    if(page==vyruz::PresentationPage::DeathPrompt){beginGameOverCard(a);return;}
    if(page==vyruz::PresentationPage::GameOver){finishGameOver(a);return;}
    if(a.presentationReturnMain){a.presentationReturnMain=false;a.menu.show(a.paused,true);a.menu.retryAvailable=a.restartLevel.has_value();a.audio.device().setMusicPaused(false);return;}
    if(a.presentationReturnExtras){a.presentationReturnExtras=false;a.menu.page=vyruz::MenuPage::Extras;a.audio.device().setMusicPaused(false);return;}
    if(page==vyruz::PresentationPage::Story && beginMovie(a))return;
    if(page==vyruz::PresentationPage::EndingTerminal) {
        beginPresentation(a,vyruz::PresentationPage::Ending,a.endingFirstText);return;
    }
    if(page==vyruz::PresentationPage::Ending) {
        beginPresentation(a,vyruz::PresentationPage::EndingSecond,a.endingSecondText);return;
    }
    if(page==vyruz::PresentationPage::EndingSecond) {
        a.sessionStarted=false;a.menu.show(a.paused,true);
    }
    a.audio.device().setMusicPaused(false);
    if(page==vyruz::PresentationPage::Mission)a.world.announceLevel();
}
void beginStartup(App& a) {
    const auto language=a.audioLanguage=="FRENCH"?"FR":"EN";
    auto text=presentationText(a,std::string("COMMON/DATA/EXTRA/STORY-")+language+".TXT");
    if(!text.empty() && loadStoryTexture(a)) {
        beginPresentation(a,vyruz::PresentationPage::Story,std::move(text));
        std::vector<uint8_t> bytes;vyruz::Wave wave;std::string error;
        const auto path=a.audioLanguage=="FRENCH"?"COMMON/DATA/SOUND/V-INTRO-FR.WAV":"COMMON/DATA/SOUND/V-INTRO-UK.WAV";
        if(a.vfs.read(path,bytes)&&vyruz::loadWav(bytes,wave))
            a.presentationVoice=a.audio.device().playNative(vyruz::makeAudioClip(std::move(wave),error),16,191,65535,0,false,true,vyruz::AudioBus::Voices);
    } else if(!beginMovie(a))a.audio.device().setMusicPaused(false);
}

std::string levelKey(std::string path) {
    for(auto& c:path) {if(c=='\\')c='/';else if(c>='a'&&c<='z')c-=32;}
    return path;
}
std::string binaryDirectory() {
    std::error_code error;
    const auto executable=std::filesystem::read_symlink("/proc/self/exe",error);
    if(!error)return executable.parent_path().string();
    if(char* base=SDL_GetBasePath()) {
        const std::string result=base;SDL_free(base);return result;
    }
    return ".";
}
std::string portableUserPath(const char* name) {
    return (std::filesystem::path(binaryDirectory())/name).string();
}
std::string defaultSavePath() { return portableUserPath("slot1.vzs"); }
std::string legacySavePath() {
    if(const char* xdg=std::getenv("XDG_DATA_HOME");xdg&&*xdg)return std::string(xdg)+"/vyruz26/slot1.vzs";
    if(const char* home=std::getenv("HOME");home&&*home)return std::string(home)+"/.local/share/vyruz26/slot1.vzs";
    return "saves/slot1.vzs";
}
void showLoadingProgress(App& a,float completed,float nextStageEnd) {
    if(!a.loadingActive)return;
    a.loadingProgress=std::max(a.loadingProgress,completed);
    a.loadingStageEnd=nextStageEnd;
    SDL_PumpEvents(); // Keep the window responsive without consuming gameplay input.
    const int pixel=static_cast<int>(325*a.loadingProgress);
    if(pixel!=a.loadingPixel) {
        a.loading.progress(a.gfx,a.loadingProgress);a.loadingPixel=pixel;
    }
}
bool readLoadingResource(App& a,const std::string& path,std::vector<uint8_t>& bytes) {
    const bool ok=a.vfs.read(path,bytes);
    if(ok && a.loadingActive)
        showLoadingProgress(a,std::min(a.loadingStageEnd,a.loadingProgress+1.f/2000),a.loadingStageEnd);
    return ok;
}
// Build and validate replacements before releasing any of the live session.
bool loadLevelData(App& a, const std::string& path,const vyruz::SavedLevel* saved=nullptr,
                   bool entering=false,bool validateOnly=false,bool present=false,int requestedDifficulty=-1,
                   const vyruz::PlayerCarry* carried=nullptr) {
    // CPU images and cursors remain available for transactional failure
    // recovery; old GPU copies need not overlap the replacement level.
    a.sceneRenderer.releaseTextures(a.gfx);
    releaseUnusedPortMemory();
    a.assistedAim={};a.aimAssistTarget=SIZE_MAX;
    a.loadError="Could not load level resources: "+path;
    // Validation-only paths also run without a graphics context. Failed loads
    // leave the live world intact and the next normal frame restores its view.
    struct LoadingScope { App& app; ~LoadingScope(){app.loadingActive=false;} } loadingScope{a};
    a.loadingActive=false;
    if(a.window && SDL_GL_GetCurrentContext()) {
        std::string loadingError;
        if(a.loading.load([&a](const auto& name,auto& bytes){return a.vfs.read(name,bytes);},loadingError)) {
            a.loading.begin();a.loadingActive=true;a.loadingProgress=0;a.loadingPixel=-1;
            showLoadingProgress(a,0,.05f);
        } else std::fprintf(stderr,"loading screen: %s\n",loadingError.c_str());
    }
    vyruz::Level nextLevel;
    vyruz::SceneContent nextContent;
    std::vector<uint8_t> data;
    if (!a.vfs.read(path, data)) {
        a.loadError="Missing level: "+path;
        std::fprintf(stderr, "level not found: %s\n", path.c_str());
        return false;
    }
    if (!vyruz::loadLevel(data, nextLevel)) {
        a.loadError="Invalid level: "+path;
        std::fprintf(stderr, "level parse failed: %s\n", path.c_str());
        return false;
    }
    const size_t dot = path.rfind('.');
    if (dot == std::string::npos) return false;
    const std::string prefix = path.substr(0, dot);
    std::vector<uint8_t> instances, catalog;
    if (!a.vfs.read(prefix + ".3DI", instances) || !a.vfs.read(prefix + ".3DX", catalog) ||
        !vyruz::loadTerrain(instances, catalog, nextLevel)) {
        a.loadError="Missing or invalid terrain: "+prefix+".3DI / .3DX";
        std::fprintf(stderr, "terrain metadata missing or invalid for %s\n", path.c_str());
        return false;
    }
    std::printf("level %s: %d tiles, %dx%d map, %d layers, %zu collision edges\n", path.c_str(),
                static_cast<int>(nextLevel.tiles.size()), nextLevel.mapW, nextLevel.mapH,
                nextLevel.layerCount, nextLevel.collisionEdges.size());

    std::string contentError;
    vyruz::NativeTileBodyCatalog nativeTiles;
    if(!vyruz::decodeNativeTileBodyCatalog(instances,
            std::string_view(reinterpret_cast<const char*>(catalog.data()),catalog.size()),nativeTiles,contentError) ||
       !vyruz::loadNativeTileBodyResources(nativeTiles,path,[&a](const auto& name,auto& bytes){return readLoadingResource(a,name,bytes);},contentError)) {
        a.loadError=contentError;return false;
    }
    showLoadingProgress(a,.05f,.2f);
    if (!vyruz::loadSceneContent([&a](const auto& name, auto& bytes) { return readLoadingResource(a,name,bytes); },
                                path, nextContent, contentError)) {
        a.loadError=contentError;
        std::fprintf(stderr,"content load failed: %s\n",contentError.c_str());
        return false;
    }
    std::printf("content: %zu classes, %zu objects, %zu paths, %zu diagnostics\n",
                nextContent.classes.size(),nextContent.objects.size(),nextContent.paths.size(),
                nextContent.diagnostics.size());
    for (const auto& diagnostic : nextContent.diagnostics)
        std::fprintf(stderr,"content: %s\n",diagnostic.c_str());

    showLoadingProgress(a,.2f,.3f);

    vyruz::SceneRenderer nextRenderer;
    nextRenderer.setNativeEnemyPhases();
    vyruz::HudRenderer nextHud;
    const auto worldName = path.substr(0,path.find_first_of("/\\"));
    vyruz::GameWorld validation;
    validation.setNativeTileCatalog(std::move(nativeTiles));
    validation.setPlayerName(a.options.playerName);validation.setHudLanguage(a.options.hudLanguage);
    nextHud.setLanguage(a.options.hudLanguage);
    validation.setEffectReader([&a](const auto& name,auto& bytes){return a.vfs.read(name,bytes);});
    if (!validation.load(nextContent,nextLevel,contentError,requestedDifficulty>=0?requestedDifficulty:a.haveLevel?a.world.difficulty():a.options.difficulty)) {
        a.loadError=contentError;return false;
    }
    const auto entry=validation.playerPosition();
    if(saved && !validation.restoreSnapshot(saved->world,contentError)) {
        a.loadError=contentError;return false;
    }
    if(carried)validation.restore(*carried);
    showLoadingProgress(a,.3f,.7f);
    if (!nextRenderer.load(nextContent, [&a](const auto& name, auto& bytes) { return readLoadingResource(a,name,bytes); }, contentError) ||
        !nextRenderer.loadTerrain(*validation.nativeTileCatalog(),contentError,validation.presentationRandomState()) ||
        (saved && !nextRenderer.restore(saved->render,contentError)) ||
        !nextHud.load([&a](const auto& name,auto& bytes){return readLoadingResource(a,name,bytes);},worldName,contentError)) {
        std::fprintf(stderr, "gameplay load failed: %s\n", contentError.c_str());
        a.loadError=contentError;nextRenderer.clear(a.gfx);return false;
    }
    if(validateOnly){nextRenderer.clear(a.gfx);showLoadingProgress(a,1,1);return true;}
    // Additional port-only validation Worlds never consume live session IDs.
    // Commit a fresh native pool reconstruction only after every asset/scene
    // check succeeds; failure leaves the active World and allocator intact.
    if(!validation.reconstructNativePoolForReplacement(a.world.nativePoolReplacementContext(),contentError,saved!=nullptr)) {
        a.loadError=contentError;nextRenderer.clear(a.gfx);return false;
    }
    // Parsed data now belongs to nextLevel/content. Drop raw input buffers
    // before atlas creation and release the old sprite cache before preloading
    // unused/new class banks. All fallible validation still precedes commit.
    std::vector<uint8_t>().swap(data);
    std::vector<uint8_t>().swap(instances);
    std::vector<uint8_t>().swap(catalog);
    showLoadingProgress(a,.75f,.85f);
    if(entering)validation.setPlayerPosition(entry.x,entry.y);
    const auto atlas=vyruz::paddedTileAtlas(nextLevel.tiles,kAtlasPerRow);
    if (a.atlasTex) a.gfx.deleteTexture(a.atlasTex);
    a.atlasW=atlas.width;a.atlasH=atlas.height;
    a.atlasTex=a.gfx.uploadTexture(atlas.rgba.data(),atlas.width,atlas.height);
    a.sceneRenderer.clear(a.gfx);
    a.level = std::move(nextLevel);
    a.content = std::move(nextContent);
    a.sceneRenderer = std::move(nextRenderer);
    releaseUnusedPortMemory();
    a.sceneRenderer.preloadClassSprites(a.content);
    nextHud.setLevel(a.level);
    if(entering)nextHud.inheritInventory(a.hud);
    a.hud.clear(a.gfx);a.hud=std::move(nextHud);a.deathPresented=false;a.presentationReturnMain=false;a.presentationReturnExtras=false;a.paused=false;a.inventory={};a.menu={};a.menu.debugEnabled=a.debugEnabled;a.menu.slidesAvailable=a.slidesAvailable;
    a.world=std::move(validation);a.world.rebind(a.content,a.level);
    a.hud.rebuildMapMarkers(a.content,a.world);
    a.presentation.close();a.audio.device().stop(a.presentationVoice);a.presentationVoice=0;
    // 43e5f0 ->445580 runs on gameplay-session entry, including save/retry.
    // Validation and tooling loads remain silent; ordinary pause/resume never reloads.
    if(!saved || entering || (present && a.world.player().alive)) {
        const auto text=present?presentationText(a,vyruz::missionTextPath(path,a.audioLanguage)):std::string{};
        if(!text.empty())beginPresentation(a,vyruz::PresentationPage::Mission,text);
        else a.world.announceLevel();
    }
    for (const auto& diagnostic : a.world.diagnostics()) std::fprintf(stderr, "gameplay: %s\n", diagnostic.c_str());
    const auto spawn = a.world.playerPosition();
    a.worldX = spawn.x; a.worldY = spawn.y; a.motion = {};a.motion.direction=a.world.player().direction;
    a.haveLevel = true;
    bindEffectCamera(a);
    showLoadingProgress(a,.9f,.99f);
    if(!saved) {
        a.restartLevel=vyruz::SavedLevel{a.world.snapshot(),a.sceneRenderer.capture()};
        a.restartPath=levelKey(a.content.mapPath);a.restartProfile=a.options.storageProfile();a.retryDiskPath.clear();
    }
    if(a.audio.device().available()) {
        std::string audioError;
        a.audio.load(a.content,[&a](const auto& name,auto& bytes){return readLoadingResource(a,name,bytes);},a.audioLanguage,audioError,a.world.nativeTileCatalog());
        for(const auto& diagnostic:a.audio.diagnostics())std::fprintf(stderr,"audio: %s\n",diagnostic.c_str());
    }
    showLoadingProgress(a,1,1);
    releaseUnusedPortMemory();
    return true;
}

bool captureLevel(App& a,vyruz::SavedLevel& saved,std::string& error) {
    if(!a.sceneRenderer.sync(a.content,error))return false;
    a.sceneRenderer.syncActors(a.world.actors());
    saved={a.world.snapshot(),a.sceneRenderer.capture()};return true;
}
#include "save_preview.inc"
void invalidateSaveCatalog(App& a);
void refreshSaveCatalog(App& a,bool force=false);
bool saveSession(App& a,int64_t cost,std::string& error) {
    if(a.demoActive){error="Demo playback cannot be saved.";return false;}
    vyruz::SaveGame save;save.currentLevel=levelKey(a.content.mapPath);save.restartLevel=a.restartLevel;
    vyruz::SavedLevel current;
    if(!captureLevel(a,current,error))return false;
    auto& money=current.world.actors.at(current.world.player).vitals.money;
    if(cost<0||money<cost){error="Not enough credits to save.";return false;}
    money-=cost;
    if(a.world.saveRequest())current.world.message.clear();
    save.levels[save.currentLevel]=std::move(current);
    std::lock_guard storage(a.saveStorageMutex); // Synchronous tooling path only.
    std::error_code ec;const auto parent=std::filesystem::path(a.savePath).parent_path();
    if(!parent.empty())std::filesystem::create_directories(parent,ec);
    if(ec){error="Cannot create save directory: "+ec.message();return false;}
    bool ok=false;
    if(a.saveHistory) {
        if(const auto path=vyruz::writeHistorySave(a.savePath,a.options.storageProfile(),save,error)) {
            ok=true;a.lastSavedPath=path->string();captureSavePreview(a,*path);
        }
    } else {ok=vyruz::writeSaveFile(a.savePath,save,error);if(ok)a.lastSavedPath=a.savePath;}
    if(ok)invalidateSaveCatalog(a);
    return ok;
}
bool startAsyncSave(App& a,int64_t cost,std::string& error) {
    try {
        auto snapshot=std::make_unique<vyruz::SaveGame>();
        snapshot->currentLevel=levelKey(a.content.mapPath);
        vyruz::SavedLevel current;
        if(!captureLevel(a,current,error))return false;
        auto& money=current.world.actors.at(current.world.player).vitals.money;
        if(cost<0||money<cost){error="Not enough credits to save.";return false;}
        money-=cost;current.world.message.clear();
        snapshot->levels.emplace(snapshot->currentLevel,std::move(current));
#if VYRUZ_GLES
        std::future<std::vector<uint8_t>> preview;
        if(a.saveHistory && a.window && !a.menu.open())try{
            auto producer=std::make_unique<std::promise<std::vector<uint8_t>>>();
            preview=producer->get_future();a.savePreviewPromise=std::move(producer);
            a.gfx.requestPreview();
        }catch(...){cancelSavePreview(a);} // Optional pixels never prevent a save.
#else
        auto preview=a.saveHistory?readSavePreview(a):std::vector<uint8_t>{};
#endif
        const auto path=a.savePath,profile=a.options.storageProfile();
        const bool history=a.saveHistory;
        a.saveFuture=std::async(std::launch::async,[&a,snapshot=std::move(snapshot),preview=std::move(preview),path,profile,history]() mutable {
            SaveResult result;
            try {
                // Destroy large working state here, never in future::get on the
                // render thread. Only the small result crosses back.
                auto save=std::move(*snapshot);snapshot.reset();
                save.restartLevel=a.restartLevel;
                {
                    std::lock_guard storage(a.saveStorageMutex);
                    std::error_code ec;const auto parent=std::filesystem::path(path).parent_path();
                    if(!parent.empty())std::filesystem::create_directories(parent,ec);
                    if(ec){result.error="Cannot create save directory: "+ec.message();return result;}
                    if(history) {
                        if(auto written=vyruz::writeHistorySave(path,profile,std::move(save),result.error)){
                            result.ok=true;result.path=written->string();
                        }
                    } else {
                        result.ok=vyruz::writeSaveFile(path,std::move(save),result.error);
                        if(result.ok)result.path=path;
                    }
                }
                // Release the storage lock before waiting for renderer pixels:
                // shutdown must be able to join a catalog reader independently.
                // Optional preview failure must never undo a committed save.
                if(result.ok)try{
#if VYRUZ_GLES
                    if(preview.valid())writeSavePreview(result.path,preview.get());
#else
                    writeSavePreview(result.path,preview);
#endif
                }catch(...){}
            }catch(const std::exception& e){result.error=e.what();}
            releaseUnusedPortMemory();
            return result;
        });
        error.clear();return true;
    }catch(const std::exception& e){cancelSavePreview(a);error=e.what();return false;}
}
bool startAsyncRetry(App& a,std::string& error) {
    try {
        releaseUnusedPortMemory();
        const auto path=a.savePath,profile=a.options.storageProfile(),level=a.restartPath;
        a.saveFuture=std::async(std::launch::async,[&a,path,profile,level] {
            SaveResult result;result.retry=true;
            try {
                vyruz::SaveGame save;save.currentLevel=level;
                save.levels[save.currentLevel]=*a.restartLevel;
                result.ok=vyruz::writeDefaultRetry(path,profile,std::move(save),result.error);
            }catch(const std::exception& e){result.error=e.what();}
            releaseUnusedPortMemory();
            return result;
        });
        error.clear();return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
bool serviceSave(App& a,bool asynchronous=false) {
    if(a.saveFuture.valid()){
        if(a.saveFuture.wait_for(std::chrono::seconds(0))!=std::future_status::ready)return false;
        const auto result=a.saveFuture.get();
        if(result.retry){
            if(!result.ok)std::fprintf(stderr,"Could not store default retry session: %s (in-memory retry retained)\n",result.error.c_str());
            return false;
        }
        if(!result.ok)cancelSavePreview(a);
        a.world.finishSave(result.ok,result.error);
        if(result.ok){a.lastSavedPath=result.path;invalidateSaveCatalog(a);refreshSaveCatalog(a);a.audio.speech("V-GAME-SAVED.WAV",500,1000,3000);}
        std::printf("save %s: %s\n",result.ok?"complete":"failed",result.ok?result.path.c_str():result.error.c_str());
        return false; // Gameplay never stopped; no clock reset/catch-up.
    }
    if(a.demoActive)return false;
    if(!a.world.saveRequest())return false;
    const auto request=*a.world.saveRequest();std::string error;
    if(asynchronous){
        if(!startAsyncSave(a,request.cost,error)){
            a.world.finishSave(false,error);std::fprintf(stderr,"save failed: %s\n",error.c_str());
        }
        return false;
    }
    const bool ok=saveSession(a,request.cost,error);a.world.finishSave(ok,error);
    if(ok)a.audio.speech("V-GAME-SAVED.WAV",500,1000,3000);
    std::printf("save %s: %s\n",ok?"complete":"failed",ok?a.lastSavedPath.c_str():error.c_str());
    return true;
}
bool loadSession(App& a,const std::string& selectedPath={}) {
    vyruz::SaveGame save;std::string error;
    const auto& source=selectedPath.empty()?a.savePath:selectedPath;
    if(!vyruz::readSaveFile(source,save,error)){a.loadError=error;return false;}
    // Legacy saves may contain completed maps. Validate them transactionally,
    // but never retain them after loading the current level.
    for(auto it=save.levels.begin();it!=save.levels.end();) {
        if(it->first==save.currentLevel){++it;continue;}
        if(!loadLevelData(a,it->first,&it->second,false,true))return false;
        it=save.levels.erase(it);
    }
    const auto current=save.levels.find(save.currentLevel);
    if(current==save.levels.end()){a.loadError="Save has no current level.";return false;}
    if(save.restartLevel && !loadLevelData(a,current->first,&*save.restartLevel,false,true))return false;
    if(!loadLevelData(a,current->first,&current->second,false,false,true))return false;
    // Legacy saves have no entry history: retry the loaded live checkpoint.
    a.restartLevel=std::move(save.restartLevel);
    if(!a.restartLevel && a.world.player().alive)a.restartLevel=current->second;
    const auto currentPath=current->first;
    a.restartPath=currentPath;a.restartProfile=a.options.storageProfile();a.retryDiskPath.clear();
    a.sessionStarted=true;a.scoreRecorded=false;a.suppressFireUntilRelease=true;
    std::printf("loaded save: %s\n",source.c_str());return true;
}
SaveCatalogResult readSaveCatalog(const vyruz::Vfs& vfs,std::string base,std::string profile,uint64_t generation) {
    SaveCatalogResult result;result.base=std::move(base);result.profile=std::move(profile);result.generation=generation;
    result.entries=vyruz::listSaveCatalog(result.base,result.profile);
    static const char* difficulty[]={"Easy","Normal","Hard","Insane"};
    std::map<std::string,std::string> names;
    for(const auto& entry:result.entries) {
        if(!entry.valid()) {
            result.labels.push_back(entry.path.filename().string()+" (unreadable)");
            result.help.push_back(entry.error);continue;
        }
        auto [name,inserted]=names.emplace(entry.map,entry.map);
        if(inserted) {
            auto path=entry.map;const auto dot=path.rfind('.');if(dot!=std::string::npos)path.replace(dot,std::string::npos,".3DO");
            std::vector<uint8_t> bytes;vyruz::TextDocument document;std::string error;
            if(vfs.read(path,bytes) && vyruz::parseContentText(std::string_view(reinterpret_cast<const char*>(bytes.data()),bytes.size()),path,document,error))
                if(const auto* title=vyruz::contentProperty(document.nodes,"level-name")) {
                    if(!title->values.empty()) {
                        name->second.clear();for(const auto& word:title->values){if(!name->second.empty())name->second+=' ';name->second+=word;}
                    } else if(title->children.size()==1 && title->children.front().kind==vyruz::TextKind::Bare) {
                        name->second=title->children.front().key;
                        for(const auto& word:title->children.front().arguments)name->second+=' '+word;
                    }
                }
        }
        const auto seconds=static_cast<uint64_t>(std::max(0.0,entry.elapsedMs)/1000);
        char time[96],help[192];
        std::snprintf(time,sizeof(time),"   [%llu h %llu mn %llu sec] ",
            static_cast<unsigned long long>(seconds/3600),static_cast<unsigned long long>(seconds/60%60),static_cast<unsigned long long>(seconds%60));
        std::snprintf(help,sizeof(help),"Energy : %d\nShield : %d\nDifficulty : %s ...\n",
            int(entry.energy),int(entry.shield),difficulty[std::clamp(entry.difficulty,0,3)]);
        result.labels.push_back(name->second+time+(entry.legacy?"(legacy)":""));result.help.push_back(help);
    }
    return result;
}
void invalidateSaveCatalog(App& a) {
    ++a.saveCatalogGeneration;a.saveCatalogDirty=true;
    a.saveCatalog.clear();a.menu.saveLabels.clear();a.menu.saveHelp.clear();
    a.menu.savedSessions=0;a.menu.savedSessionsLoading=true;
    // A queued confirmation must never reuse an index from an older list.
    a.menu.selectedSave=-1;
    a.gfx.deleteTexture(a.savePreviewTexture);a.savePreviewTexture=0;a.savePreviewPath.clear();
    if(a.menu.page==vyruz::MenuPage::SavedSessions)a.menu.selected=int(a.menu.entries().size())-1;
}
void refreshSaveCatalog(App& a,bool force) {
    const auto base=a.savePath,profile=a.options.storageProfile();
    if(a.saveCatalogBase!=base || a.saveCatalogProfile!=profile) {
        invalidateSaveCatalog(a);a.saveCatalogBase=base;a.saveCatalogProfile=profile;
    } else if(force && !a.saveCatalogDirty)invalidateSaveCatalog(a);
    // Reads already in flight may finish, but never publish across a save write.
    if(a.saveFuture.valid())return;
    if(a.saveCatalogFuture.valid()) {
        if(a.saveCatalogFuture.wait_for(std::chrono::seconds(0))!=std::future_status::ready)return;
        auto result=a.saveCatalogFuture.get();
        if(result.generation==a.saveCatalogGeneration && result.base==base && result.profile==profile) {
            a.saveCatalog=std::move(result.entries);a.menu.saveLabels=std::move(result.labels);a.menu.saveHelp=std::move(result.help);
            a.saveCatalogDirty=false;a.menu.savedSessionsLoading=false;a.menu.savedSessions=int(a.saveCatalog.size());
            if(a.menu.page==vyruz::MenuPage::SavedSessions) {
                a.menu.selected=int(a.menu.entries().size())-1;
                if(!result.error.empty())a.menu.error="Cannot read saved sessions: "+result.error;
                else if(a.saveCatalog.empty())a.menu.error="No saved session.";
                else a.menu.error.clear();
            }
        }
    }
    if(base.empty()){a.saveCatalogDirty=false;a.menu.savedSessionsLoading=false;}
    if(!a.saveCatalogDirty)return;
    const auto generation=a.saveCatalogGeneration;
    // No World, renderer, GL, options or menu access from the worker. Full save
    // decode/validation, title reads and large temporary destruction stay here.
    try {
        a.saveCatalogFuture=std::async(std::launch::async,[vfs=&a.vfs,storage=&a.saveStorageMutex,base,profile,generation] {
            try{std::lock_guard lock(*storage);return readSaveCatalog(*vfs,base,profile,generation);}
            catch(const std::exception& error){SaveCatalogResult result;result.base=base;result.profile=profile;result.generation=generation;result.error=error.what();return result;}
        });
    } catch(const std::exception& error) {
        a.saveCatalogDirty=false;a.menu.savedSessionsLoading=false;
        if(a.menu.page==vyruz::MenuPage::SavedSessions)a.menu.error="Cannot start saved-session refresh: "+std::string(error.what());
    }
}
#include "retry_session.inc"

void recordScore(App& a);
bool changeLevel(App& a,const vyruz::TransitionRequest& request) {
    if(vyruz::endingDestination(request.path)) {
        const auto language=a.audioLanguage=="FRENCH"?"FR":"EN";
        auto first=presentationText(a,std::string("COMMON/DATA/EXTRA/THEEND-")+language+".TXT");
        auto second=presentationText(a,std::string("COMMON/DATA/EXTRA/THEEND-")+language+"-2.TXT");
        std::vector<uint8_t> bytes;vyruz::Image image;
        if(first.empty() || second.empty() || !a.vfs.read("COMMON/DATA/EXTRA/THEEND.TGA",bytes) || !vyruz::loadTga(bytes,image)) {
            a.world.rejectTransition("Original ending resources are missing or invalid.");return false;
        }
        if(!a.endingTex)a.endingTex=a.gfx.uploadTexture(image.rgba.data(),image.width,image.height);
        //445dd0 calls446120 only on Final-object completion, before dispatch.
        const bool terminal=request.path=="*" && request.lift<a.world.actors().size() && a.world.actors()[request.lift].finalObject;
        a.endingFirstText=std::move(first);a.endingSecondText=std::move(second);a.world.finishEnding();a.inventory.open=false;
        recordScore(a);a.sessionStarted=false;
        beginPresentation(a,terminal?vyruz::PresentationPage::EndingTerminal:vyruz::PresentationPage::Ending,
            terminal?vyruz::endingTerminalText(a.audioLanguage):a.endingFirstText);return true;
    }
    const bool debugAI=a.world.debugEnemyAI(),debugGod=a.world.debugGodMode();
    // Native41f7c0 replaces the previous world. Campaign exits are forward-only;
    // keep player carry, not a snapshot of every completed map. loadLevelData
    // validates the destination before committing, preserving failure rollback.
    auto carried=a.world.carry();
    const auto destination=levelKey(request.path);
    carried.vitals.money-=request.cost;
    if(!loadLevelData(a,destination,nullptr,true,false,true,-1,&carried)) {
        a.world.rejectTransition(a.loadError);return false;
    }
    a.world.setDebugEnemyAI(debugAI);a.world.setDebugGodMode(debugGod);
    a.restartLevel=vyruz::SavedLevel{a.world.snapshot(),a.sceneRenderer.capture()};
    a.restartPath=levelKey(a.content.mapPath);a.restartProfile=a.options.storageProfile();
    persistRetrySession(a,true);
    return true;
}

bool restartLevel(App& a) {
    if(!a.retryDiskPath.empty()){const auto file=a.retryDiskPath;return loadSession(a,file);}
    const auto path=a.restartLevel && !a.restartPath.empty()?a.restartPath:a.content.mapPath;
    // Copy before load commits: the checkpoint is independent of the dying world.
    const auto checkpoint=a.restartLevel;
    if(!loadLevelData(a,path,checkpoint?&*checkpoint:nullptr,false,false,true))return false;
    if(checkpoint)a.restartLevel=checkpoint;
    a.scoreRecorded=false;a.sessionStarted=true;a.suppressFireUntilRelease=true;
    return true;
}

#include "extras_ui.inc"
#include "demo_playback.inc"

void refreshOptions(App& a);
bool selectDebugLevel(App& a,size_t selected) {
    if(!a.debugEnabled)return false;
    if(selected>=vyruz::debugLevels().size())return false;
    const bool ai=a.world.debugEnemyAI(),god=a.world.debugGodMode();
    // Same staged loader as normal gameplay: failures retain the live session.
    // Fresh test session, no departing-level capture, reward, or disk write.
    if(!loadLevelData(a,vyruz::debugLevels()[selected],nullptr,false,false,true))return false;
    a.sessionStarted=true;a.suppressFireUntilRelease=true;
    a.world.setDebugEnemyAI(ai);a.world.setDebugGodMode(god);
    return true;
}
bool applyMenuAction(App& a) {
    if(a.saveFuture.valid())return false; // Keep queued load/restart actions until the checkpoint commits.
    bool levelChanged=false;
        if(a.menuAction!=vyruz::MenuAction::None) {
            const auto action=a.menuAction;a.menuAction=vyruz::MenuAction::None;
            if(!a.debugEnabled && action>=vyruz::MenuAction::Debug && action<=vyruz::MenuAction::DebugSelectSurprise)return false;
            if(action==vyruz::MenuAction::Options){a.optionsPage=0;a.menu.showOptions();refreshOptions(a);}
            else if(action==vyruz::MenuAction::Demo){levelChanged=startDemo(a);if(!levelChanged)a.menu.error="Could not play original demo: "+a.loadError;}
            else if(action==vyruz::MenuAction::Scores||action==vyruz::MenuAction::Credits||action==vyruz::MenuAction::Intro||action==vyruz::MenuAction::SlideShow||action==vyruz::MenuAction::ControlsHelp)beginExtra(a,action);
            else if(action==vyruz::MenuAction::DebugAI)a.world.setDebugEnemyAI(!a.world.debugEnemyAI());
            else if(action==vyruz::MenuAction::DebugGod)a.world.setDebugGodMode(!a.world.debugGodMode());
            else if(action==vyruz::MenuAction::DebugAmmo){a.world.debugFillAmmo();a.menu.error="All ammunition filled.";}
            else if(action==vyruz::MenuAction::DebugMoney)a.menu.error=a.world.debugAddMoney()?"Added 1,000,000 money.":"Balance limit: no money added.";
            else if(action==vyruz::MenuAction::DebugSelectSurprise){
                if(a.world.debugSetSurprise(static_cast<vyruz::SurpriseKind>(a.menu.selected))) {
                    a.menu.close(a.paused);a.paused=false;a.suppressFireUntilRelease=true;
                } else a.menu.error="Restart the level to test a Surprise.";
            }
            else if(action==vyruz::MenuAction::DebugSelectLevel){
                levelChanged=selectDebugLevel(a,a.menu.selected);
                if(!levelChanged)a.menu.error="Could not load level: "+a.loadError;
            }
            else if(action==vyruz::MenuAction::Quit)a.quit=1;
            else if(action==vyruz::MenuAction::EndGame) {
                a.menu.close(a.paused);a.menu.show(a.paused,true);a.sessionStarted=false;a.menu.retryAvailable=a.restartLevel.has_value();a.audio.device().stopEffects();
            } else if(action==vyruz::MenuAction::LoadGame) {
                refreshSaveCatalog(a,true);a.menu.showSavedSessions();
                if(!a.menu.savedSessionsLoading && a.saveCatalog.empty())a.menu.error="No saved session.";
            } else if(action==vyruz::MenuAction::LoadSelected) {
                const auto index=a.menu.selectedSave;
                if(index>=0 && size_t(index)<a.saveCatalog.size())levelChanged=loadSession(a,a.saveCatalog[index].path.string());
                else a.loadError="Select a saved session.";
                if(!levelChanged)a.menu.error="Could not load save: "+a.loadError;
            } else if(action==vyruz::MenuAction::NewGame || action==vyruz::MenuAction::Restart) {
                const std::string path=action==vyruz::MenuAction::NewGame?"UNDERGROUND/T1U1.3DG":a.content.mapPath;
                levelChanged=action==vyruz::MenuAction::Restart?restartLevel(a):
                    loadLevelData(a,path,nullptr,false,false,true,a.options.difficulty);
                if(levelChanged){
                    if(action==vyruz::MenuAction::NewGame){a.scoreRecorded=false;persistRetrySession(a,true);}
                    a.sessionStarted=true;a.suppressFireUntilRelease=true;
                }
                else a.menu.error="Could not load level. Try again.";
            }
        }
    return levelChanged;
}

bool saveScreenshot(const char* path, int w, int h) {
    if(w<=0 || h<=0 || w>32768 || h>32768) {
        std::fprintf(stderr,"invalid screenshot dimensions: %dx%d\n",w,h);return false;
    }
    std::vector<uint8_t> px(static_cast<size_t>(w) * h * 3);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
#if VYRUZ_GLES
    std::vector<uint8_t> rgba(size_t(w)*h*4);
    glReadPixels(0,0,w,h,GL_RGBA,GL_UNSIGNED_BYTE,rgba.data());
    for(size_t i=0;i<size_t(w)*h;++i)for(int c=0;c<3;++c)px[i*3+c]=rgba[i*4+c];
#else
    glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, px.data());
#endif
    if(glGetError()!=GL_NO_ERROR) {std::fprintf(stderr,"screenshot framebuffer read failed\n");return false;}
    FILE* f = std::fopen(path, "wb");
    if (!f) {std::fprintf(stderr,"cannot create screenshot: %s\n",path);return false;}
    bool ok=std::fprintf(f, "P6\n%d %d\n255\n", w, h)>0;
    for (int y = h - 1; y >= 0 && ok; --y) {
        const auto bytes=static_cast<size_t>(w)*3;
        ok=std::fwrite(px.data() + static_cast<size_t>(y) * bytes, 1, bytes, f)==bytes;
    }
    if(std::fclose(f)!=0)ok=false;
    if(!ok){std::fprintf(stderr,"screenshot write failed: %s\n",path);return false;}
    std::printf("saved screenshot %s\n", path);
    return true;
}

bool findPlayerStart(const App& a, float& wx, float& wy) {
    if (!a.content.player) return false;
    const auto& player = a.content.objects[a.content.player->value];
    wx = player.position.x;
    wy = player.position.y;
    return true;
}

// Geometric --walk probe: same boundaries, legacy slide response. Live input
// uses the physical tick path; this probe does not validate bounce/momentum.
bool tryMove(App& a, float mx, float my, float speed = 3.0f) {
    if (mx == 0.f && my == 0.f) return false;
    const float len = std::sqrt(mx * mx + my * my);
    const float nx = a.worldX + mx / len * speed;
    const float ny = a.worldY + my / len * speed;
    a.world.movePlayer(nx - a.worldX, ny - a.worldY);
    const auto position = a.world.playerPosition();
    const bool moved = position.x != a.worldX || position.y != a.worldY;
    a.worldX = position.x; a.worldY = position.y;
    return moved;
}

#include "options_ui.inc"

// The original dead-player countdown leaves ordinary world simulation running.
void advanceDeathPrompt(App& a,double milliseconds) {
    if(a.presentation.page!=vyruz::PresentationPage::DeathPrompt || a.presentation.suspended)return;
    bindEffectCamera(a);
    a.world.tick(static_cast<float>(milliseconds/1000.0),{});
    a.presentation.advance(milliseconds);
}

void scrollControlsHelp(App& a,int delta,Uint64 now,bool tap=false){
    if(!delta||(!tap&&now<a.helpNextScroll))return;
    a.helpFirst=std::clamp(a.helpFirst+delta,0,std::max(0,int(a.helpRows.size())-9));
    a.helpNextScroll=now+100; //43ab50 held-scroll cooldown; no catch-up burst.
}
void applyControllerAimAssist(App& a,vyruz::MovementCommand& command,bool stickActive) {
    const auto view=a.gfx.viewport();
    a.assistedAim={};
    if(!a.mouseUsed && a.controller.connected() && !a.controllerBlockedUntilNeutral && a.options.aimAssist && (command.aimActive||a.options.aimAssist==4)){
        std::vector<vyruz::AimCandidate> targets;
        const auto origin=a.world.playerPosition();
        const auto& actors=a.world.actors();
        for(size_t i=0;i<actors.size()&&i<a.content.objects.size();++i){
            const auto& actor=actors[i];const auto& object=a.content.objects[i];
            if(!actor.enemy || !actor.alive || actor.vitals.energy<=0 || actor.turretOnlyBase || actor.effect || actor.pickup)continue;
            if(object.flags.invisible.value_or(false)&&!actor.enemyRevealed)continue;
            const float x=object.position.x-origin.x,y=object.position.y-origin.y;
            if(std::abs(x)>view.width/2-24 || std::abs(y)>view.height/2-24)continue;
            // Visibility for this controller aid only; do not alter native AI
            // scanners or projectile collision. Water shores are not walls.
            if(!vyruz::terrainMoveAllowed(a.level,origin.x,origin.y,
                                         object.position.x,object.position.y,.01f,true))continue;
            targets.push_back({i,x,y});
        }
        a.assistedAim=vyruz::assistAim(a.options.aimAssist,command.aimX,command.aimY,
                                     stickActive,a.options.aimAssist==4?a.world.retaliationTarget():a.aimAssistTarget,targets);
        a.aimAssistTarget=a.assistedAim.target;
        if(a.assistedAim.target!=SIZE_MAX)command.aimActive=true;
        command.aimX=a.assistedAim.x;command.aimY=a.assistedAim.y;
    }else a.aimAssistTarget=SIZE_MAX;
}

void handleInput(App& a) {
    int windowW=0,windowH=0;SDL_GetWindowSize(a.window,&windowW,&windowH);
    a.gfx.resize(windowW,windowH);
    const auto view=a.gfx.viewport();
    const bool wasMenu=a.menu.open(),wasInventory=a.inventory.open,wasMap=a.world.mapOpen(),wasPaused=a.paused;
    const int wasSelected=a.menu.selected;
    int weaponDelta = 0;
    const bool modal=a.presentation.open();
    bool dismissed=false;
    const auto* held=SDL_GetKeyboardState(nullptr);
    const bool heldConfirm=held[SDL_SCANCODE_SPACE]||held[SDL_SCANCODE_RETURN]||held[SDL_SCANCODE_KP_ENTER]||held[SDL_SCANCODE_ESCAPE]||
        (SDL_GetMouseState(nullptr,nullptr)&SDL_BUTTON_LMASK)||a.controller.confirmationHeld();
    if(modal&&a.inputFocused&&!heldConfirm)a.presentationArmed=true;
    if(a.inputFocused&&a.controllerBlockedUntilNeutral&&a.controller.controlsNeutral())a.controllerBlockedUntilNeutral=false;
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        if(a.controller.event(ev)){
            a.suppressFireUntilRelease=true;a.controllerBlockedUntilNeutral=true;
            a.menuHold.reset();a.menuStick.reset();a.mouseUsed=false;a.padAimX=a.padAimY=0;a.assistedAim={};a.aimAssistTarget=SIZE_MAX;a.presentationArmed=false;
            if(a.bindingCapture==2)optionsBack(a);
            if(a.sessionStarted&&!a.menu.open()&&!modal&&!a.demoActive&&a.world.endingRemainingMilliseconds()<=0)a.menu.show(a.paused,false);
        }
        const bool stickMenu=a.menu.open()||a.inventory.open||a.presentation.page==vyruz::PresentationPage::ControlsHelp;
        if(!stickMenu)a.menuStick.reset();
        if(ev.type==SDL_CONTROLLERAXISMOTION && stickMenu && (!a.bindingCapture||a.bindingCapture==5) &&
           a.controller.owns(ev.caxis.which) && !a.controllerBlockedUntilNeutral){
            const int oldHorizontal=ev.caxis.axis==SDL_CONTROLLER_AXIS_LEFTX?a.menuStick.x:0;
            if(a.menuStick.translate(ev)){
                if(oldHorizontal)a.menuHold.release(true,oldHorizontal);
                a.menuPointerVisible=false;
            }
        }
        if(ev.type==SDL_CONTROLLERBUTTONDOWN&&(!a.controller.owns(ev.cbutton.which)||a.controllerBlockedUntilNeutral))continue;
        if(ev.type==SDL_WINDOWEVENT&&ev.window.event==SDL_WINDOWEVENT_FOCUS_LOST){
            a.inputFocused=false;a.presentationArmed=false;
            a.menuHold.reset();a.menuStick.reset();a.controller.resetAim();a.mouseUsed=false;a.padAimX=a.padAimY=0;a.assistedAim={};a.aimAssistTarget=SIZE_MAX;
            a.controllerBlockedUntilNeutral=a.controller.connected();a.suppressFireUntilRelease=true;
            if(a.bindingCapture)optionsBack(a);
        }
        if(ev.type==SDL_WINDOWEVENT&&ev.window.event==SDL_WINDOWEVENT_FOCUS_GAINED){
            a.inputFocused=true;a.presentationArmed=false;
            a.controllerBlockedUntilNeutral=a.controller.connected();a.suppressFireUntilRelease=true;
        }
        if(!a.inputFocused&&ev.type!=SDL_QUIT&&ev.type!=SDL_WINDOWEVENT)continue;
        if(ev.type==SDL_KEYDOWN||ev.type==SDL_MOUSEMOTION||ev.type==SDL_MOUSEWHEEL||ev.type==SDL_MOUSEBUTTONDOWN||ev.type==SDL_CONTROLLERBUTTONDOWN||ev.type==SDL_CONTROLLERAXISMOTION)a.mainMenuIdleSince=SDL_GetTicks64();
        if(a.demoActive){
            if(ev.type==SDL_QUIT)a.quit=true;
            if((ev.type==SDL_KEYDOWN&&!ev.key.repeat)||ev.type==SDL_MOUSEBUTTONDOWN||ev.type==SDL_CONTROLLERBUTTONDOWN){stopDemo(a);return;}
            continue;
        }
        if(modal) {
            if(ev.type==SDL_QUIT)a.quit=true;
            if(ev.type==SDL_WINDOWEVENT) {
                if(ev.window.event==SDL_WINDOWEVENT_FOCUS_LOST)a.presentation.suspended=true;
                if(ev.window.event==SDL_WINDOWEVENT_FOCUS_GAINED)a.presentation.suspended=false;
            }
            if(a.presentation.page==vyruz::PresentationPage::ControlsHelp) {
                int delta=0;
                if(ev.type==SDL_KEYDOWN){if(ev.key.keysym.sym==SDLK_UP)delta=-1;if(ev.key.keysym.sym==SDLK_DOWN)delta=1;}
                if(ev.type==SDL_MOUSEWHEEL)delta=-wheelSteps(ev.wheel);
                if(ev.type==SDL_CONTROLLERBUTTONDOWN){if(ev.cbutton.button==SDL_CONTROLLER_BUTTON_DPAD_UP)delta=-1;if(ev.cbutton.button==SDL_CONTROLLER_BUTTON_DPAD_DOWN)delta=1;}
                if(!a.presentation.suspended)scrollControlsHelp(a,delta,SDL_GetTicks64(),ev.type==SDL_MOUSEWHEEL||(ev.type==SDL_KEYDOWN&&!ev.key.repeat)||ev.type==SDL_CONTROLLERBUTTONDOWN);
                const bool closeKey=ev.type==SDL_KEYDOWN&&!ev.key.repeat&&(ev.key.keysym.sym==SDLK_ESCAPE||ev.key.keysym.sym==SDLK_SPACE||ev.key.keysym.sym==SDLK_RETURN||ev.key.keysym.sym==SDLK_KP_ENTER);
                const bool closeButton=ev.type==SDL_CONTROLLERBUTTONDOWN&&(ev.cbutton.button==SDL_CONTROLLER_BUTTON_A||ev.cbutton.button==SDL_CONTROLLER_BUTTON_B||ev.cbutton.button==SDL_CONTROLLER_BUTTON_START);
                if(!dismissed&&!a.presentation.suspended&&a.presentationArmed&&(closeKey||closeButton||(ev.type==SDL_MOUSEBUTTONDOWN&&ev.button.button==SDL_BUTTON_LEFT))){finishPresentation(a);dismissed=true;}
                continue;
            }
            const bool mission=a.presentation.page==vyruz::PresentationPage::Mission;
            const bool reveal=mission&&a.presentation.revealing();
            const auto code=ev.type==SDL_KEYDOWN?ev.key.keysym.sym:SDLK_UNKNOWN;
            const bool revealKey=code==SDLK_ESCAPE||code==SDLK_SPACE||code==SDLK_RETURN||code==SDLK_KP_ENTER||
                (ev.type==SDL_KEYDOWN&&ev.key.keysym.scancode==a.options.keyBindings[static_cast<size_t>(vyruz::OptionAction::Fire)]);
            const bool key=ev.type==SDL_KEYDOWN&&!ev.key.repeat&&(!reveal||revealKey);
            const bool button=ev.type==SDL_CONTROLLERBUTTONDOWN&&a.controller.owns(ev.cbutton.which)&&(!reveal||ev.cbutton.button==a.options.controllerBindings[static_cast<size_t>(vyruz::OptionAction::Fire)]||ev.cbutton.button==SDL_CONTROLLER_BUTTON_A||ev.cbutton.button==SDL_CONTROLLER_BUTTON_B);
            const bool click=ev.type==SDL_MOUSEBUTTONDOWN &&
                (ev.button.button==SDL_BUTTON_LEFT || (mission&&ev.button.button==a.options.fireButton) || a.presentation.page==vyruz::PresentationPage::DeathPrompt || a.presentation.page==vyruz::PresentationPage::EndingTerminal || a.presentation.page==vyruz::PresentationPage::GameOver || a.presentation.page==vyruz::PresentationPage::Scores);
            if(!dismissed&&!a.presentation.suspended&&a.presentationArmed&&(key||button||click)) {
                if(mission)a.presentation.requestDismiss();else finishPresentation(a);dismissed=true;
            }
            continue;
        }
        if(a.world.endingRemainingMilliseconds()>0) {
            if(ev.type==SDL_QUIT)a.quit=true;
            continue;
        }
        if(a.menu.open()) {
            // 421780/451e30: mouse activity reveals the rocket; keyboard
            // row navigation hides it until the mouse is used again.
            if(ev.type==SDL_MOUSEMOTION || ev.type==SDL_MOUSEBUTTONDOWN || ev.type==SDL_MOUSEBUTTONUP || ev.type==SDL_MOUSEWHEEL)a.menuPointerVisible=true;
            if(ev.type==SDL_KEYDOWN && (ev.key.keysym.sym==SDLK_UP || ev.key.keysym.sym==SDLK_DOWN))a.menuPointerVisible=false;
        }
        if(a.menu.page==vyruz::MenuPage::Options && optionsEvent(a,ev))continue;
        if(ev.type==SDL_CONTROLLERBUTTONDOWN && a.controller.owns(ev.cbutton.which)) {
            // Translate semantic controller actions into the existing UI path.
            SDL_Keycode key=0;
            switch(ev.cbutton.button) {
                case SDL_CONTROLLER_BUTTON_DPAD_UP:key=SDLK_UP;break;
                case SDL_CONTROLLER_BUTTON_DPAD_DOWN:key=SDLK_DOWN;break;
                case SDL_CONTROLLER_BUTTON_DPAD_LEFT:key=SDLK_LEFT;break;
                case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:key=SDLK_RIGHT;break;
                case SDL_CONTROLLER_BUTTON_A:key=a.menu.open()?SDLK_RETURN:(!a.world.player().alive?SDLK_r:0);break;
                case SDL_CONTROLLER_BUTTON_B:key=SDLK_ESCAPE;break;
                case SDL_CONTROLLER_BUTTON_START:key=SDLK_ESCAPE;break;
                case SDL_CONTROLLER_BUTTON_BACK:key=SDLK_m;break;
                case SDL_CONTROLLER_BUTTON_Y:key=SDLK_i;break;
                case SDL_CONTROLLER_BUTTON_X:key=SDLK_p;break;
                case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:key=SDLK_q;break;
                case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:key=SDLK_e;break;
            }
            if(!a.menu.open()&&!(a.inventory.open&&(ev.cbutton.button==SDL_CONTROLLER_BUTTON_DPAD_UP||ev.cbutton.button==SDL_CONTROLLER_BUTTON_DPAD_DOWN))) {
                key=(ev.cbutton.button==SDL_CONTROLLER_BUTTON_START||ev.cbutton.button==SDL_CONTROLLER_BUTTON_B)?SDLK_ESCAPE:
                    (ev.cbutton.button==SDL_CONTROLLER_BUTTON_A&&!a.world.player().alive)?SDLK_r:0;
                for(size_t i=0;i<vyruz::optionActionCount;++i)if(a.options.controllerBindings[i]==ev.cbutton.button) {
                    switch(static_cast<vyruz::OptionAction>(i)) {
                    case vyruz::OptionAction::NextWeapon:key=SDLK_e;break;
                    case vyruz::OptionAction::PrevWeapon:key=SDLK_q;break;
                    case vyruz::OptionAction::Map:key=SDLK_m;break;
                    case vyruz::OptionAction::Inventory:key=SDLK_i;break;
                    case vyruz::OptionAction::Pause:key=SDLK_p;break;
                    default:break;
                    }
                }
            }
            if(key){ev={};ev.type=SDL_KEYDOWN;ev.key.keysym.sym=key;}
        }
        if(ev.type==SDL_WINDOWEVENT && ev.window.event==SDL_WINDOWEVENT_FOCUS_LOST) {
            a.suppressFireUntilRelease=true;
            if(a.sessionStarted && !a.menu.open())a.menu.show(a.paused,false);
        }
        if(a.debugEnabled && ev.type==SDL_KEYDOWN && ev.key.keysym.sym==SDLK_F2 && !ev.key.repeat && a.sessionStarted) {
            if(a.inventory.open)a.inventory.close(a.paused);
            a.menu.showDebug(a.paused);a.suppressFireUntilRelease=true;continue;
        }
        if(a.menu.open() && ev.type!=SDL_QUIT) {
            a.suppressFireUntilRelease=true;
            if(a.menu.page==vyruz::MenuPage::Controls){
                const int delta=ev.type==SDL_MOUSEWHEEL?-wheelSteps(ev.wheel):
                    ev.type==SDL_KEYDOWN?(ev.key.keysym.sym==SDLK_UP?-1:ev.key.keysym.sym==SDLK_DOWN?1:0):0;
                if(delta){if(ev.type==SDL_KEYDOWN)a.menuPointerVisible=false;const auto now=SDL_GetTicks64();
                    if(ev.type==SDL_MOUSEWHEEL||!ev.key.repeat||now>=a.helpNextScroll){a.menu.move(delta);a.helpNextScroll=now+100;}
                    continue;
                }
            }
            if(ev.type==SDL_KEYDOWN && (ev.key.keysym.sym==SDLK_UP || ev.key.keysym.sym==SDLK_DOWN))a.menuPointerVisible=false;
            if(ev.type==SDL_MOUSEMOTION || ev.type==SDL_MOUSEBUTTONDOWN) {
                const auto point=a.gfx.mousePoint(ev.type==SDL_MOUSEMOTION?ev.motion.x:ev.button.x,ev.type==SDL_MOUSEMOTION?ev.motion.y:ev.button.y);
                const float x=point.x-view.uiX(),y=point.y-view.uiY();
                if(a.menu.hover(x,y) && ev.type==SDL_MOUSEBUTTONDOWN && ev.button.button==SDL_BUTTON_LEFT)
                    a.menuAction=a.menu.activate(a.paused);
            } else if(ev.type==SDL_MOUSEWHEEL)a.menu.move(-wheelSteps(ev.wheel));
            else if(ev.type==SDL_KEYDOWN&&(!ev.key.repeat ||
                ((a.menu.page==vyruz::MenuPage::Debug||a.menu.page==vyruz::MenuPage::DebugLevels||a.menu.page==vyruz::MenuPage::DebugSurprises)&&
                 (ev.key.keysym.sym==SDLK_UP||ev.key.keysym.sym==SDLK_DOWN)))) {
                // Debug tools retain their existing held-arrow browsing.
                if(ev.key.keysym.sym==SDLK_UP)a.menu.move(-1);
                else if(ev.key.keysym.sym==SDLK_DOWN)a.menu.move(1);
                else if((ev.key.keysym.sym==SDLK_RETURN||ev.key.keysym.sym==SDLK_KP_ENTER||ev.key.keysym.sym==SDLK_SPACE) && !ev.key.repeat)a.menuAction=a.menu.activate(a.paused);
                else if((ev.key.keysym.sym==SDLK_ESCAPE||ev.key.keysym.sym==SDLK_BACKSPACE) && !ev.key.repeat)a.menu.back(a.paused);
            }
            continue;
        }
        if(a.inventory.open&&ev.type==SDL_KEYDOWN&&(ev.key.keysym.sym==SDLK_UP||ev.key.keysym.sym==SDLK_DOWN||ev.key.keysym.sym==SDLK_ESCAPE)){
            if(ev.key.keysym.sym==SDLK_ESCAPE)a.inventory.close(a.paused);
            else a.inventory.scroll(ev.key.keysym.sym==SDLK_UP?-1:1,vyruz::inventoryRows(a.world.player()).size(),9);
            continue;
        }
        if(ev.type==SDL_KEYDOWN && ev.key.keysym.scancode!=SDL_SCANCODE_UNKNOWN) {
            const auto original=ev.key.keysym.sym;
            if(original==SDLK_i||original==SDLK_m||original==SDLK_p||original==SDLK_q||original==SDLK_e)ev.key.keysym.sym=0;
            for(size_t i=0;i<vyruz::optionActionCount;++i)if(a.options.keyBindings[i]==ev.key.keysym.scancode) {
                ev.key.keysym.sym=0;
                switch(static_cast<vyruz::OptionAction>(i)) {
                case vyruz::OptionAction::NextWeapon:ev.key.keysym.sym=SDLK_e;break;
                case vyruz::OptionAction::PrevWeapon:ev.key.keysym.sym=SDLK_q;break;
                case vyruz::OptionAction::Map:ev.key.keysym.sym=SDLK_m;break;
                case vyruz::OptionAction::Inventory:ev.key.keysym.sym=SDLK_i;break;
                case vyruz::OptionAction::Pause:ev.key.keysym.sym=SDLK_p;break;
                default:break;
                }
            }
        }
        switch (ev.type) {
            case SDL_QUIT: a.quit = 1; break;
            case SDL_MOUSEMOTION: a.mouseUsed = true; break;
            case SDL_KEYDOWN:
                if(ev.key.keysym.sym==SDLK_i && !ev.key.repeat && a.world.player().alive) {if(a.world.mapOpen())a.world.toggleMap();a.inventory.toggle(a.paused);break;}
                if(a.inventory.open) {
                    if(ev.key.keysym.sym==SDLK_ESCAPE) a.inventory.close(a.paused);
                    else if(ev.key.keysym.sym==SDLK_UP || ev.key.keysym.sym==SDLK_DOWN)
                        a.inventory.scroll(ev.key.keysym.sym==SDLK_UP?-1:1,vyruz::inventoryRows(a.world.player()).size(),9);
                    break;
                }
                if(ev.key.keysym.sym==SDLK_m && !ev.key.repeat && !a.paused) {a.world.toggleMap();break;}
                if(ev.key.keysym.sym==SDLK_ESCAPE && a.world.mapOpen()) {a.world.toggleMap();break;}
                if (ev.key.keysym.sym == SDLK_p && !ev.key.repeat && a.world.player().alive) {
                    if(a.world.mapOpen())a.world.toggleMap();
                    else a.paused=!a.paused;
                }
                if (ev.key.keysym.sym == SDLK_ESCAPE && !ev.key.repeat) {a.menu.show(a.paused,!a.sessionStarted);a.suppressFireUntilRelease=true;break;}
                if (ev.key.keysym.sym == SDLK_r && !a.world.player().alive) a.restart = true;
                if (ev.key.keysym.sym == SDLK_q && !ev.key.repeat) --weaponDelta;
                if (ev.key.keysym.sym == SDLK_e && !ev.key.repeat) ++weaponDelta;
                if (ev.key.keysym.sym == SDLK_F1 && !ev.key.repeat) a.showInfo = !a.showInfo;
                break;
            case SDL_MOUSEWHEEL:
                if(a.inventory.open)a.inventory.scroll(-wheelSteps(ev.wheel),vyruz::inventoryRows(a.world.player()).size(),9);
                else weaponDelta += wheelSteps(ev.wheel);
                break;
            default: break;
        }
    }

    serviceOptionsHold(a,SDL_GetTicks64());
    if(a.menu.page==vyruz::MenuPage::Controls && a.inputFocused && SDL_GetTicks64()>=a.helpNextScroll){
        const bool pad=!a.controllerBlockedUntilNeutral;
        const bool up=held[SDL_SCANCODE_UP]||(pad&&a.controller.button(SDL_CONTROLLER_BUTTON_DPAD_UP));
        const bool down=held[SDL_SCANCODE_DOWN]||(pad&&a.controller.button(SDL_CONTROLLER_BUTTON_DPAD_DOWN));
        if(up||down){a.menu.move(up?-1:1);a.helpNextScroll=SDL_GetTicks64()+100;}
    }
    if(a.inputFocused&&a.controllerBlockedUntilNeutral&&a.controller.controlsNeutral())a.controllerBlockedUntilNeutral=false;
    if(a.demoActive){advanceDemo(a,kMoveDt*1000);return;}
    if(modal){
        if(a.presentation.page==vyruz::PresentationPage::ControlsHelp&&a.inputFocused&&!a.presentation.suspended){
            const bool pad=!a.controllerBlockedUntilNeutral;
            const bool up=held[SDL_SCANCODE_UP]||(pad&&a.controller.button(SDL_CONTROLLER_BUTTON_DPAD_UP));
            const bool down=held[SDL_SCANCODE_DOWN]||(pad&&a.controller.button(SDL_CONTROLLER_BUTTON_DPAD_DOWN));
            scrollControlsHelp(a,up?-1:down?1:0,SDL_GetTicks64());
        }
        if(!dismissed&&a.inputFocused&&!a.presentation.suspended&&a.presentationArmed&&!a.controllerBlockedUntilNeutral&&a.controller.firing()&&a.presentation.page!=vyruz::PresentationPage::ControlsHelp){if(a.presentation.page==vyruz::PresentationPage::Mission)a.presentation.requestDismiss();else finishPresentation(a);}
        return;
    }
    if(!wasMenu && a.menu.open())a.audio.cue("V-MENU.WAV",true);
    else if(!wasInventory && a.inventory.open)a.audio.cue("V-INVENTORY.WAV",true);
    else if(!wasMap && a.world.mapOpen())a.audio.speech("V-MAP.WAV",0,1000,5000);
    else if(!wasPaused && a.paused)a.audio.cue("V-PAUSE.WAV",true);
    if(a.options.menuSounds && wasMenu && (wasSelected!=a.menu.selected || a.menuAction!=vyruz::MenuAction::None))a.audio.cue("select.wav");
    if (!a.inputFocused || a.paused || !a.world.player().alive) return;

    // Forward/back and strafe are independent of the pointer. The cursor
    // specifies a facing target, not a destination or a thrust command.
    const Uint8* keys = SDL_GetKeyboardState(nullptr);
    vyruz::MovementCommand command;
    const auto down=[&](vyruz::OptionAction action){const auto i=static_cast<size_t>(action);return (a.options.keyBindings[i]>0 && keys[a.options.keyBindings[i]]) || (!a.controllerBlockedUntilNeutral&&a.controller.button(a.options.controllerBindings[i]));};
    const auto alias=[&](SDL_Scancode key,vyruz::OptionAction action) {
        const auto found=std::find(a.options.keyBindings.begin(),a.options.keyBindings.end(),int(key));
        return keys[key] && (found==a.options.keyBindings.end() || found-a.options.keyBindings.begin()==static_cast<int>(action));
    };
    command.forward=int(down(vyruz::OptionAction::Forward)||alias(SDL_SCANCODE_UP,vyruz::OptionAction::Forward))-int(down(vyruz::OptionAction::Reverse)||alias(SDL_SCANCODE_DOWN,vyruz::OptionAction::Reverse));
    command.strafe=int(down(vyruz::OptionAction::Right)||alias(SDL_SCANCODE_RIGHT,vyruz::OptionAction::Right))-int(down(vyruz::OptionAction::Left)||alias(SDL_SCANCODE_LEFT,vyruz::OptionAction::Left));
    const auto controller=a.controllerBlockedUntilNeutral?vyruz::MovementCommand{}:a.controller.movement(kMoveDt,a.world.player().direction,a.mouseUsed);
    command.forward+=controller.forward;
    command.strafe+=controller.strafe;
    if(controller.aimActive){a.mouseUsed=false;a.padAimX=controller.aimX;a.padAimY=controller.aimY;}

    int imx = 0, imy = 0;
    const Uint32 buttons = SDL_GetMouseState(&imx, &imy);
    const bool fire=(a.options.fireButton && (buttons&SDL_BUTTON(a.options.fireButton)))||down(vyruz::OptionAction::Fire)||(!a.controllerBlockedUntilNeutral&&a.controller.firing(a.options.controllerBindings[static_cast<size_t>(vyruz::OptionAction::Fire)]));
    if(a.options.thrustButton && (buttons&SDL_BUTTON(a.options.thrustButton)))command.forward+=1;
    if(!fire)a.suppressFireUntilRelease=false;
    const auto mousePoint=a.gfx.mousePoint(imx,imy);
    a.mouseX=mousePoint.x;a.mouseY=mousePoint.y;
    command.aimActive = a.mouseUsed;
    command.aimX = a.mouseX - view.width / 2.f;
    command.aimY = a.mouseY - view.height / 2.f;
    if(!a.mouseUsed && (a.padAimX!=0||a.padAimY!=0)) {
        command.aimActive=true;command.aimX=a.padAimX;command.aimY=a.padAimY;
    }
    applyControllerAimAssist(a,command,controller.aimActive);
    if (a.forcedDir >= 0) a.world.setPlayerDirection(a.forcedDir);
    bindEffectCamera(a);
    const auto worldStart=a.profileTick?SDL_GetPerformanceCounter():0;
    a.world.tick(kMoveDt, {command, fire && !a.suppressFireUntilRelease, weaponDelta});
    if(a.profileTick)a.profileWorldMs=1000.0*(SDL_GetPerformanceCounter()-worldStart)/SDL_GetPerformanceFrequency();
    const auto position = a.world.playerPosition();
    a.worldX = position.x; a.worldY = position.y;
    a.motion.direction = a.world.player().direction;
}

void endingColor(App& a,double remaining) {
    int offset=0;
    if(remaining>2500) {
        const int flash=static_cast<int>((4500-remaining)/250);
        if(flash!=a.endingFlash){a.endingFlash=flash;offset=255;a.audio.cue("electric.wav");}
    } else {
        // 446060: logarithmic white-to-black gamma offset over2500ms.
        const double factor=std::log(1+(std::exp(1.0)-1)*remaining/2500);
        offset=static_cast<int>(510*factor)-255;
    }
    if(!offset)return;
    // 460160 gamma-ramp transfer, adapted to the framebuffer in OpenGL.
    const float opacity=offset>0?offset/256.f:-offset/256.f;
    glDisable(GL_TEXTURE_2D);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(offset>0?1.f:0.f,offset>0?1.f:0.f,offset>0?1.f:0.f,opacity);
    const auto view=a.gfx.viewport();glBegin(GL_QUADS);
    glVertex2f(0,0);glVertex2f(view.width,0);glVertex2f(view.width,view.height);glVertex2f(0,view.height);glEnd();
    glEnable(GL_TEXTURE_2D);glColor4f(1,1,1,1);
}
// Presentation-only diagnostics. Half-second averages include swap and pacing;
// peak exposes stalls without adding GL queries or synchronizing the GPU.
void recordDebugFrame(App& a,double milliseconds) {
    if(!a.debugEnabled && !a.options.fpsCounter){
        a.debugFrameSum=0;a.debugFramePeak=0;a.debugFrameCount=0;a.fpsText="FPS --";return;
    }
    if(milliseconds<=0)return;
    a.debugFrameSum+=milliseconds;++a.debugFrameCount;
    a.debugFramePeak=std::max(a.debugFramePeak,milliseconds);
    if(a.debugFrameSum>=500){
        char label[128];std::snprintf(label,sizeof(label),"FPS %.1f  FRAME %.2f MS  MAX %.2f MS",
            1000.0*a.debugFrameCount/a.debugFrameSum,a.debugFrameSum/a.debugFrameCount,a.debugFramePeak);
        a.debugFrameText=label;
        std::snprintf(label,sizeof(label),"FPS %.1f",1000.0*a.debugFrameCount/a.debugFrameSum);a.fpsText=label;
        a.debugFrameSum=0;a.debugFramePeak=0;a.debugFrameCount=0;
    }
}
void presentApplication(App& a) {
    if((a.debugEnabled || a.options.fpsCounter) && SDL_GetTicks64()>=a.debugMemoryNext){
        a.debugMemoryNext=SDL_GetTicks64()+1000;
        const auto memory=vyruz::processMemory(a.debugEnabled);
        const auto mib=[](std::optional<uint64_t> kib){
            if(!kib)return std::string("N/A");
            char text[32];std::snprintf(text,sizeof(text),"%.1f",double(*kib)/1024);return std::string(text);
        };
        a.rssText="RSS "+mib(memory.rss)+(memory.rss?" MIB":"");
        if(a.debugEnabled){
        a.debugMemoryText={
            "RAM MIB: RSS "+mib(memory.rss)+"  PSS "+mib(memory.pss),
            "ANON "+mib(memory.anonymous)+"  FILE "+mib(memory.file)+"  SHMEM "+mib(memory.shmem),
            "PRIVATE "+mib(memory.privatePages)+"  SHARED "+mib(memory.sharedPages)+" (ALT VIEW)"};
        const auto& actors=a.world.actors();
        const auto live=std::count_if(actors.begin(),actors.end(),[](const auto& actor){return actor.alive;});
        a.debugMemoryText.push_back("OBJECTS "+std::to_string(actors.size())+"  LIVE "+std::to_string(live)+"  DEAD "+std::to_string(actors.size()-live));
        char storage[96];std::snprintf(storage,sizeof(storage),"ROW DATA %.1f MIB (EXCLUDES DYNAMIC DATA)",
            double(actors.size()*sizeof(vyruz::ActorState)+a.content.objects.size()*sizeof(vyruz::ObjectPlacement))/(1024*1024));
        a.debugMemoryText.push_back(storage);
        }
    }
    if(a.debugEnabled || a.options.fpsCounter){
        const float right=a.debugEnabled?480.f:230.f;
        glMatrixMode(GL_MODELVIEW);glPushMatrix();glLoadIdentity();
        glPushAttrib(GL_ENABLE_BIT|GL_COLOR_BUFFER_BIT|GL_CURRENT_BIT);
        glDisable(GL_TEXTURE_2D);glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
        glColor4f(0,0,0,.65f);glBegin(GL_QUADS);
        const float bottom=100+(a.debugEnabled?20.f*a.debugMemoryText.size():0);
        glVertex2f(10,72);glVertex2f(right,72);glVertex2f(right,bottom);glVertex2f(10,bottom);glEnd();
        glEnable(GL_TEXTURE_2D);glColor4f(1,1,1,1);
        a.hud.drawMenuText(a.gfx,a.debugEnabled?a.debugFrameText:a.fpsText+"  "+a.rssText,18,94,4,right-20);
        if(a.debugEnabled)for(size_t i=0;i<a.debugMemoryText.size();++i)
            a.hud.drawMenuText(a.gfx,a.debugMemoryText[i],18,114+20.f*i,4,right-20);
        glPopAttrib();glPopMatrix();
    }
    a.gfx.present();
    finishSavePreview(a);
}

// Presentation only: stick aim remains the unchanged direction vector passed
// to World. A fixed logical radius keeps the crosshair readable at any aspect.
bool gameplayCrosshair(App& a,SDL_FPoint& point) {
    if(!a.options.crosshair || a.presentation.open() || !a.haveLevel || a.world.mapOpen() ||
       a.inventory.open || a.menu.open())return false;
    if(a.mouseUsed){point={a.mouseX,a.mouseY};return true;}
    if(!a.controller.connected() || a.controllerBlockedUntilNeutral)return false;
    if(a.options.aimAssist && a.assistedAim.target!=SIZE_MAX){
        const auto view=a.gfx.viewport();
        point={view.width/2+a.assistedAim.x,view.height/2+a.assistedAim.y};return true;
    }
    float x=a.padAimX,y=a.padAimY;
    const float length=std::hypot(x,y);
    if(length>0){x/=length;y/=length;}
    else {
        constexpr float pi=3.14159265358979323846f;
        const float angle=3*pi/4-a.world.player().direction*pi/12;
        x=std::cos(angle);y=std::sin(angle);
    }
    const auto view=a.gfx.viewport();
    point={view.width/2+120*x,view.height/2+120*y};
    return true;
}

void render(App& a,float alpha=1) {
    SDL_FPoint crosshair{};
    const bool gameCursor=gameplayCrosshair(a,crosshair);
    const bool hiddenGameplayCursor=!a.options.crosshair&&a.haveLevel&&!a.world.mapOpen()&&!a.inventory.open;
    SDL_ShowCursor((gameCursor||hiddenGameplayCursor||a.menu.open()||a.presentation.open())?SDL_DISABLE:SDL_ENABLE);
    a.sceneRenderer.reactorFx=a.options.reactorFx;a.sceneRenderer.groundDetails=a.options.groundDetails;a.sceneRenderer.shadows=a.options.shadows;
    a.gfx.beginFrame(kVirtualW, kVirtualH);
    a.gfx.clear(0.03f, 0.03f, 0.05f);
    const auto view=a.gfx.viewport();
    if(a.presentation.page==vyruz::PresentationPage::Credits || a.presentation.page==vyruz::PresentationPage::Scores || a.presentation.page==vyruz::PresentationPage::SlideShow || a.presentation.page==vyruz::PresentationPage::GameOver){drawExtras(a);return;}
    const double endingRemaining=a.world.endingRemainingMilliseconds();
    if(endingRemaining<=0) {a.gfx.deleteTexture(a.endingFrameTex);a.endingFrameTex=0;a.endingFlash=-1;}
    if(a.presentation.page==vyruz::PresentationPage::EndingTerminal) {
        a.gfx.clear(0,0,0);glPushMatrix();glTranslatef(view.uiX(),view.uiY(),0);
        a.hud.drawPresentation(a.gfx,a.presentation);glPopMatrix();presentApplication(a);return;
    }
    if(endingRemaining>0 && endingRemaining<=2500 && a.endingFrameTex && !a.presentation.open()) {
        a.gfx.beginQuads(a.endingFrameTex);
        a.gfx.quad(0,0,0,1,view.width,0,1,1,view.width,view.height,1,0,0,view.height,0,0);a.gfx.endQuads();
        endingColor(a,endingRemaining);presentApplication(a);return;
    }
    if(a.presentation.page==vyruz::PresentationPage::Story || a.presentation.page==vyruz::PresentationPage::Movie ||
       a.presentation.page==vyruz::PresentationPage::Ending || a.presentation.page==vyruz::PresentationPage::EndingSecond) {
        a.gfx.clear(0,0,0);
        GLuint texture=(a.presentation.page==vyruz::PresentationPage::Ending || a.presentation.page==vyruz::PresentationPage::EndingSecond)?a.endingTex:a.storyTex;float w=640,h=480;
        if(a.presentation.page==vyruz::PresentationPage::Movie) {
            const double clock=a.audio.device().playbackMilliseconds(a.presentationVoice).value_or(a.presentation.elapsedMilliseconds());
            const size_t frame=std::min(a.intro.frameCount()-1,static_cast<size_t>(clock*a.intro.frameRate()/1000));
            if(frame!=a.movieFrame) {
                vyruz::Image image;std::string error;
                if(!a.intro.frame(frame,image,error)) {
                    std::fprintf(stderr,"intro frame failed: %s\n",error.c_str());finishPresentation(a);presentApplication(a);return;
                }
                if(!a.movieTex)a.movieTex=a.gfx.uploadTexture(image.rgba.data(),image.width,image.height);
                else {glBindTexture(GL_TEXTURE_2D,a.movieTex);glTexSubImage2D(GL_TEXTURE_2D,0,0,0,image.width,image.height,GL_RGBA,GL_UNSIGNED_BYTE,image.rgba.data());}
                a.movieFrame=frame;
            }
            texture=a.movieTex;
            const float scale=std::min(view.width/a.intro.width(),view.height/a.intro.height());
            w=a.intro.width()*scale;h=a.intro.height()*scale;
        }
        const float x=(view.width-w)/2,y=(view.height-h)/2;
        a.gfx.beginQuads(texture);a.gfx.quad(x,y,0,0,x+w,y,1,0,x+w,y+h,1,1,x,y+h,0,1);a.gfx.endQuads();
        glTranslatef(view.uiX(),view.uiY(),0);a.hud.drawPresentation(a.gfx,a.presentation);
        presentApplication(a);return;
    }
    if(a.menu.open()){
        if(a.menu.page==vyruz::MenuPage::Options && (a.optionsPage==1||a.optionsPage==8))refreshOptions(a);
        if(a.menu.page==vyruz::MenuPage::Options && a.bindingCapture==5)
            a.menu.optionLabels[0]="Player: "+a.nameEdit;
        glTranslatef(view.uiX(),view.uiY(),0);
        refreshMenuContext(a);
        a.menu.debugAI=a.world.debugEnemyAI();a.menu.debugGod=a.world.debugGodMode();
        const bool controlsHelp=a.presentation.page==vyruz::PresentationPage::ControlsHelp;
        a.hud.drawMenu(a.gfx,a.menu,controlsHelp);
        if(a.menu.page==vyruz::MenuPage::Options && a.bindingCapture==5 &&
           ((SDL_GetTicks64()-a.nameEditBlinkStart)/500)%2==0)
            a.hud.drawNameCaret(a.menu.optionLabels[0],a.menu.rowLeft(),a.menu.rowTop());
        if(controlsHelp)a.hud.drawControlsHelp(a.gfx,a.helpRows,a.helpFirst);
        else {drawSavePreview(a);a.hud.drawMenuScanline(a.gfx);}
        if(a.menuPointerVisible) {
            int pointerX=0,pointerY=0;SDL_GetMouseState(&pointerX,&pointerY);
            const auto point=a.gfx.mousePoint(pointerX,pointerY);
            a.hud.drawMenuCursor(a.gfx,point.x-view.uiX(),point.y-view.uiY());
        }
        presentApplication(a);return;
    }

    if (a.haveLevel) {
        // Camera: player world pixel at viewport centre.
        vyruz::ContentPosition player{a.worldX,a.worldY};
        if(a.world.playerIndex()<a.previousPositions.size())
            player=vyruz::presentationPosition(a.previousPositions[a.world.playerIndex()],player,alpha);
        a.camX = player.x - view.width / 2.f;
        a.camY = player.y - view.height / 2.f;
        if(a.world.surprise().feedbackMs>0) {
            a.camX+=a.world.surprise().shakeOffset.x;
            a.camY+=a.world.surprise().shakeOffset.y;
        }
#if VYRUZ_GLES
        // Keep baked tiles and scenery overlays on one raster grid while the
        // interpolated camera moves. Simulation/player interpolation is intact.
        a.camX=vyruz::Viewport::pixelAligned(a.camX,view.width,a.gfx.drawableWidth());
        a.camY=vyruz::Viewport::pixelAligned(a.camY,view.height,a.gfx.drawableHeight());
#endif

        const auto& L = a.level;
        // Visible cell range (margin for tile extents).
        const int tx0 = std::max(0, static_cast<int>((a.camX - 64.f) / kTileW));
        const int tx1 = std::min(L.mapW - 1, static_cast<int>((a.camX + view.width + 64.f) / kTileW));
        const int ty0 = std::max(0, static_cast<int>((a.camY - 48.f) / kTileH));
        const int ty1 = std::min(L.mapH - 1, static_cast<int>((a.camY + view.height + 48.f) / kTileH));

        const auto drawTiles = [&]() {
            a.gfx.beginQuads(a.atlasTex);
            for (int layer = 0; layer < L.layerCount; ++layer) {
                const auto& grid = L.layers[layer];
                for (int ty = ty0; ty <= ty1; ++ty) {
                    for (int tx = tx0; tx <= tx1; ++tx) {
                        const uint32_t idx = grid[static_cast<size_t>(ty) * L.mapW + tx];
                        if (idx == 0 || idx > L.tiles.size()) continue;
                        const float x0 = tx * kTileW - a.camX;
                        const float y0 = ty * kTileH - a.camY;
                        const int ti = static_cast<int>(idx) - 1;
                        const float u0 = static_cast<float>(vyruz::tileAtlasX(ti,kAtlasPerRow)) / a.atlasW;
                        const float v0 = static_cast<float>(vyruz::tileAtlasY(ti,kAtlasPerRow)) / a.atlasH;
                        const float u1 = static_cast<float>(vyruz::tileAtlasX(ti,kAtlasPerRow)+kTileW) / a.atlasW;
                        const float v1 = static_cast<float>(vyruz::tileAtlasY(ti,kAtlasPerRow)+kTileH) / a.atlasH;
                        a.gfx.quad(x0, y0, u0, v0,
                                   x0 + kTileW, y0, u1, v0,
                                   x0 + kTileW, y0 + kTileH, u1, v1,
                                   x0, y0 + kTileH, u0, v1);
                    }
                }
            }
            a.gfx.endQuads();
        };
        drawTiles();

        // One native phase0 before composition, including its timed bank
        // updates. Darkness's second player-only draw must not repeat this.
        // Fixed-step simulation and presentation can run at different rates.
        // Consume World101c time once; a redraw must not replay the last tick.
        const double enemyDrawMs=a.enemyDrawStepMs;a.enemyDrawStepMs=0;
        a.sceneRenderer.advanceEnemyPhase(enemyDrawMs,a.world.actors(),
            enemyAnimationView(a,a.camX,a.camY),vyruz::EnemyAnimationPhase::Draw);
        a.world.consumePlayerHitForRender();
        a.sceneRenderer.draw(a.gfx, a.content, L, a.camX, a.camY,
                             player.x, player.y, a.motion.direction, a.forcedStep,
                             [&](const vyruz::ForegroundTile& tile) {
            const float x = tile.x * kTileW - a.camX;
            const float y = tile.y * kTileH - a.camY;
            const float u = static_cast<float>(vyruz::tileAtlasX(tile.tile,kAtlasPerRow)) / a.atlasW;
            const float v = static_cast<float>(vyruz::tileAtlasY(tile.tile,kAtlasPerRow)) / a.atlasH;
            const float uEnd = u + static_cast<float>(kTileW) / a.atlasW;
            const float vEnd = v + static_cast<float>(kTileH) / a.atlasH;
            a.gfx.beginQuads(a.atlasTex);
            a.gfx.quad(x, y, u, v, x + kTileW, y, uEnd, v,
                       x + kTileW, y + kTileH, uEnd, vEnd,
                       x, y + kTileH, u, vEnd);
            a.gfx.endQuads();
        }, &a.world.actors(),&a.previousPositions,alpha,&a.world.surprise());
    } else if (a.storyTex) {
        glPushMatrix();glTranslatef(view.uiX(),view.uiY(),0);
        a.gfx.beginQuads(a.storyTex);
        a.gfx.quad(0, 0, 0, 0, kVirtualW, 0, 1, 0, kVirtualW, kVirtualH, 1, 1, 0, kVirtualH, 0, 1);
        a.gfx.endQuads();glPopMatrix();
    }

    if (a.haveLevel) {
        a.hud.drawSurpriseWorld(a.gfx,a.world,a.camX,a.camY);
        if(a.world.surprise().active(vyruz::SurpriseKind::Darkness)) {
            // Original 43e5f0 restores the clip and draws mask3 actors AFTER
            // darkzone: the player remains visible outside the lit window.
            auto player=a.world.playerPosition();
            if(a.world.playerIndex()<a.previousPositions.size())
                player=vyruz::presentationPosition(a.previousPositions[a.world.playerIndex()],player,alpha);
            a.sceneRenderer.draw(a.gfx,a.content,a.level,a.camX,a.camY,player.x,player.y,
                a.motion.direction,a.forcedStep,[](const vyruz::ForegroundTile&){},
                &a.world.actors(),&a.previousPositions,alpha,&a.world.surprise(),true);
        }
        a.gfx.applySceneEffects();
        if(a.options.vignette)a.hud.drawScreenBorders(a.gfx);
        if(!a.presentation.open())a.hud.drawNotifications(a.gfx,a.world,a.camX,a.camY);
        // The paid full-map overlay is authored at4:3, like inventory and menus;
        // retain its projection instead of stretching terrain with HUD edges.
        const auto hud=a.world.mapOpen()?vyruz::HudLayout::fit(view,2):a.gfx.hudLayout();
        glPushMatrix();glTranslatef(hud.x,hud.y,0);glScalef(hud.scale,hud.scale,1);
        a.hud.draw(a.gfx,a.world,a.content,a.paused && !a.inventory.open && !a.menu.open(),hud.width,hud.height);
        a.hud.drawSurpriseStatus(a.gfx,a.world,hud.width,hud.height);
        glPopMatrix();
        // Menus, inventory and authored presentation stay in their original
        // centered safe area; HUD aspect never changes world/camera projection.
        glPushMatrix();glTranslatef(view.uiX(),view.uiY(),0);
        a.hud.drawInventory(a.gfx,a.world,a.inventory,640,480,optionKeyName(a.options.keyBindings[static_cast<size_t>(vyruz::OptionAction::Inventory)]));
        // Open menus are rendered by the earlier branch. Refreshing their
        // context here decoded every retained save after each gameplay save.
        a.hud.drawPresentation(a.gfx,a.presentation);
        glPopMatrix();
        if(a.world.player().alive && a.world.player().vitals.energy>0 && !a.presentation.open() &&
           !a.menu.open() && !a.inventory.open && !a.world.mapOpen())
            if(const auto center=a.sceneRenderer.playerBodyCenter();center &&
               vyruz::coveredPlayerCenter(a.level,center->x,center->y)) {
                auto p=a.world.playerPosition();
                if(a.world.playerIndex()<a.previousPositions.size())
                    p=vyruz::presentationPosition(a.previousPositions[a.world.playerIndex()],p,alpha);
                a.hud.drawPlayerIndicator(a.gfx,p.x-a.camX,p.y-a.camY,a.motion.direction);
            }
    }

    if(a.demoActive)a.hud.drawDemoOverlay(a.gfx);
    if(a.presentation.page==vyruz::PresentationPage::DeathPrompt)a.hud.drawDeathPrompt(a.gfx);

    // Crosshair.
    if (gameCursor) {
        a.hud.drawCursor(a.gfx,crosshair.x,crosshair.y);
    }

    if(endingRemaining>0) {
        if(!a.endingFrameTex){glGenTextures(1,&a.endingFrameTex);glBindTexture(GL_TEXTURE_2D,a.endingFrameTex);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);}
        glBindTexture(GL_TEXTURE_2D,a.endingFrameTex);
        glCopyTexImage2D(GL_TEXTURE_2D,0,GL_RGB,0,0,a.gfx.drawableWidth(),a.gfx.drawableHeight(),0);
        endingColor(a,endingRemaining);
    }
    if(a.saveFuture.valid())
        a.hud.drawMenuText(a.gfx,std::string("Saving")+std::string(1+(SDL_GetTicks64()/350)%3,'.'),view.width/2-42,view.height-38,4,160);
    presentApplication(a);
}

} // namespace

int main(int argc, char** argv) {
    SDL_SetMainReady();
    if(argc==2 && std::string_view(argv[1])=="--licenses"){for(const char* notice:vyruz::videoLicenseChunks)std::fputs(notice,stdout);std::fputs(vyruz::crtLicenses,stdout);std::fputs(vyruz::smaaLicense,stdout);return 0;}
    const std::string executableDirectory=binaryDirectory();
    std::string dataDir=executableDirectory;
    if(const char* supplied=std::getenv("VYRUZ_DATA_DIR");supplied&&*supplied)dataDir=supplied;
    if(const char* supplied=std::getenv("VURUS_DATA_DIR");supplied&&*supplied)dataDir=supplied;
    std::string optionsPath=defaultOptionsPath();
    bool checkData=false,checkAllData=false,debugEnabled=false;
    std::string levelPath = "Underground\\T1U1.3DG";
    bool mute=false,noMusic=false;float volume=.7f;
    std::string musicDir,audioLanguage="ENGLISH";int musicTrack=2,musicTheme=1;bool musicTrackExplicit=false;
    std::string controllerMap;
    std::string shotPath,savePath=defaultSavePath(),loadPath,writePath;
    bool explicitSavePath=false;
    bool probePause=false,probeInventory=false;float probeDamage=0;
    int profileFrames=0,profileWarmup=120,renderHz=60;
    bool renderHzOverride=false;
    bool havePlayerOverride = false;
    float playerOverrideX = 0.f, playerOverrideY = 0.f;
    int walkSteps = 0;
    float walkDx = 1.f, walkDy = 0.f;
    int forcedDir = -1, forcedStep = -1;
    double animationMs = 0;
    int simulateMs = 0;
    bool simulateFire = false,probeMap=false;
    int64_t probeMoney=-1;
    std::string probeMenu;bool directPlay=false;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--data") && i + 1 < argc) dataDir = argv[++i];
        else if (!std::strcmp(argv[i], "--options-file") && i+1<argc)optionsPath=argv[++i];
        else if (!std::strcmp(argv[i], "--check-data"))checkData=true;
        else if (!std::strcmp(argv[i], "--check-data-all"))checkData=checkAllData=true;
        else if (!std::strcmp(argv[i], "--debug"))debugEnabled=true;
        else if (!std::strcmp(argv[i], "--mute"))mute=true;
        else if (!std::strcmp(argv[i], "--no-music"))noMusic=true;
        else if (!std::strcmp(argv[i], "--music-dir") && i+1<argc)musicDir=argv[++i];
        else if (!std::strcmp(argv[i], "--music-theme") && i+1<argc){
            musicTheme=std::atoi(argv[++i]);
            if(musicTheme<1||musicTheme>3){std::fprintf(stderr,"--music-theme must be 1..3\n");return 1;}
        }
        else if (!std::strcmp(argv[i], "--music-track") && i+1<argc){
            musicTrack=std::atoi(argv[++i]);musicTrackExplicit=true;
            if(musicTrack<2||musicTrack>8){std::fprintf(stderr,"--music-track must be 2..8\n");return 1;}
        }
        else if (!std::strcmp(argv[i], "--volume") && i+1<argc){
            char* end=nullptr;volume=std::strtof(argv[++i],&end);
            if(!std::isfinite(volume)||volume<0||volume>1||end==argv[i]||*end){std::fprintf(stderr,"--volume must be 0..1\n");return 1;}
        }
        else if (!std::strcmp(argv[i], "--audio-language") && i+1<argc){
            audioLanguage=levelKey(argv[++i]);
            if(audioLanguage!="ENGLISH"&&audioLanguage!="FRENCH"){std::fprintf(stderr,"--audio-language expects english or french\n");return 1;}
        }
        else if (!std::strcmp(argv[i], "--save-file") && i+1<argc){savePath=argv[++i];explicitSavePath=true;}
        else if (!std::strcmp(argv[i], "--load-save") && i+1<argc) loadPath=argv[++i];
        else if (!std::strcmp(argv[i], "--write-save") && i+1<argc) writePath=argv[++i];
        else if (!std::strcmp(argv[i], "--controller-map") && i+1<argc)controllerMap=argv[++i];
        else if (!std::strcmp(argv[i], "--fps") && i+1<argc){
            const std::string value=argv[++i];
            if(value!="60"&&value!="90"){std::fprintf(stderr,"--fps expects 60 or 90\n");return 1;}
            renderHz=std::stoi(value);renderHzOverride=true;
        }
        else if (!std::strcmp(argv[i], "--profile-frames") && i+1<argc) profileFrames=std::clamp(std::atoi(argv[++i]),0,36000);
        else if (!std::strcmp(argv[i], "--profile-warmup") && i+1<argc) profileWarmup=std::clamp(std::atoi(argv[++i]),0,36000);
        else if (!std::strcmp(argv[i], "--play")) directPlay=true;
        else if (!std::strcmp(argv[i], "--menu") && i+1<argc) {
            probeMenu=argv[++i];
            if(probeMenu!="main"&&probeMenu!="game"&&probeMenu!="controls"&&probeMenu!="confirm") {std::fprintf(stderr,"--menu expects main, game, controls or confirm\n");return 1;}
        }
        else if (!std::strcmp(argv[i], "--map")) probeMap=true;
        else if (!std::strcmp(argv[i], "--money") && i+1<argc) probeMoney=std::clamp<int64_t>(std::strtoll(argv[++i],nullptr,10),0,1000000000);
        else if (!std::strcmp(argv[i], "--inventory")) probeInventory=true;
        else if (!std::strcmp(argv[i], "--pause")) probePause=true;
        else if (!std::strcmp(argv[i], "--damage") && i+1<argc) probeDamage=std::max(0.f,std::strtof(argv[++i],nullptr));
        else if (!std::strcmp(argv[i], "--level") && i + 1 < argc) levelPath = argv[++i];
        else if (!std::strcmp(argv[i], "--shot") && i + 1 < argc) shotPath = argv[++i];
        else if (!std::strcmp(argv[i], "--fire")) simulateFire = true;
        else if (!std::strcmp(argv[i], "--simulate-ms") && i + 1 < argc) {
            simulateMs = std::atoi(argv[++i]);
            if (simulateMs < 0 || simulateMs > 60000) { std::fprintf(stderr,"--simulate-ms must be 0..60000\n"); return 1; }
        }
        else if (!std::strcmp(argv[i], "--anim-ms") && i + 1 < argc) {
            animationMs = std::atof(argv[++i]);
            if (!std::isfinite(animationMs) || animationMs < 0 || animationMs > 60000) {
                std::fprintf(stderr, "--anim-ms must be between 0 and 60000\n"); return 1;
            }
        }
        else if (!std::strcmp(argv[i], "--player") && i + 2 < argc) {
            playerOverrideX = static_cast<float>(std::atof(argv[++i]));
            playerOverrideY = static_cast<float>(std::atof(argv[++i]));
            havePlayerOverride = true;
        } else if (!std::strcmp(argv[i], "--walk") && i + 1 < argc) {
            walkSteps = std::atoi(argv[++i]);
        } else if (!std::strcmp(argv[i], "--walkdir") && i + 2 < argc) {
            walkDx = static_cast<float>(std::atof(argv[++i]));
            walkDy = static_cast<float>(std::atof(argv[++i]));
        } else if (!std::strcmp(argv[i], "--dir") && i + 1 < argc) {
            forcedDir = std::atoi(argv[++i]);
        } else if (!std::strcmp(argv[i], "--step") && i + 1 < argc) {
            forcedStep = std::atoi(argv[++i]);
        } else if (!std::strcmp(argv[i], "--help") || !std::strcmp(argv[i], "-h")) {
            std::printf("usage: %s [--data <directory|ISO|BIN|CUE>] [--check-data|--check-data-all] [--level <path>] [--player WX WY]"
                        " [--walk N] [--walkdir DX DY] [--dir 0..23]"
                        " [--debug] [--step raw-bitmap-index] [--anim-ms 0..60000] [--simulate-ms 0..60000] [--fire] [--pause] [--inventory] [--map] [--money N] [--play] [--menu main|game|controls|confirm] [--mute] [--volume 0..1] [--no-music] [--music-dir path] [--music-track 2..8] [--music-theme 1..3] [--audio-language english|french] [--save-file path] [--load-save path] [--write-save path] [--damage N] [--controller-map path] [--fps 60|90] [--options-file path] [--profile-frames N] [--profile-warmup N] [--shot out.ppm]\n", argv[0]);
            return 0;
        } else {std::fprintf(stderr,"Unknown option or missing value: %s; use --help\n",argv[i]);return 1;}
    }

    const auto reportDataError=[&](const std::string& message){
        std::fprintf(stderr,"Game data: %s\n",message.c_str());
        const char* driver=std::getenv("SDL_VIDEODRIVER");
        if(!checkData && (!driver || (std::strcmp(driver,"offscreen") && std::strcmp(driver,"dummy"))))
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,"Vurus game data",message.c_str(),nullptr);
    };
    vyruz::DataLocation location;std::string dataError;
    std::fprintf(stderr,"Checking game data in %s...\n",dataDir.c_str());
    const auto cacheRoot=vyruz::defaultImportRoot(executableDirectory);
    const bool prepared=checkData?vyruz::prepareData(dataDir,cacheRoot,location,dataError):
        vyruz::prepareDataWithScreen(dataDir,cacheRoot,location,dataError,controllerMap);
    if(!prepared){std::fprintf(stderr,"Game data: %s\n",dataError.c_str());return 1;}
    dataDir=location.directory;
    App a;
    a.debugEnabled=debugEnabled;a.menu.debugEnabled=debugEnabled;
    if(!a.vfs.init(dataDir)){reportDataError(a.vfs.error());return 1;}
    {std::vector<uint8_t> slide; a.slidesAvailable=a.vfs.read("slides/slide-001.tga",slide);a.menu.slidesAvailable=a.slidesAvailable;}
    if(checkData){
        if(!vyruz::verifyImportedData(dataDir,dataError)){
            std::fprintf(stderr,"Game data: %s\n",dataError.c_str());return 1;
        }
        const auto levels=checkAllData?vyruz::debugLevels():std::vector<std::string>{levelPath};
        for(const auto& path:levels){
            if(!loadLevelData(a,path,nullptr,false,true)){
                std::fprintf(stderr,"Game data is incomplete or invalid: %s\nResolved directory: %s\n",a.loadError.c_str(),dataDir.c_str());return 1;
            }
            std::printf("Game data verified for %s: %s\n",path.c_str(),dataDir.c_str());
        }
        std::printf("Verified %zu level(s).\n",levels.size());return 0;
    }
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
#if VYRUZ_GLES
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#else
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
#endif

    if(!controllerMap.empty() && SDL_GameControllerAddMappingsFromFile(controllerMap.c_str())<0){
        std::fprintf(stderr,"controller mapping failed: %s\n",SDL_GetError());SDL_Quit();return 1;
    }
    const auto importLegacy=[&](const std::string& source,const std::string& destination) {
        std::string error;
        if(!vyruz::importLegacyUserFile(source,destination,error))
            std::fprintf(stderr,"Could not copy existing user file to %s: %s (original retained)\n",destination.c_str(),error.c_str());
    };
    if(optionsPath==defaultOptionsPath())importLegacy(legacyOptionsPath(),optionsPath);
    if(loadPath.empty() && savePath==defaultSavePath())importLegacy(legacySavePath(),savePath);
    importLegacy((std::filesystem::path(legacySavePath()).parent_path()/"vz.sco").string(),portableUserPath("vz.sco"));
    a.optionsPath=optionsPath;std::string optionsError;
    if(!vyruz::loadOptions(optionsPath,a.options,optionsError))std::fprintf(stderr,"options: %s; defaults retained\n",optionsError.c_str());
    a.controller.scan();
    a.savePath=loadPath.empty()?savePath:loadPath;
    a.saveHistory=!explicitSavePath && loadPath.empty();
    a.audioLanguage=audioLanguage;
    a.forcedDir = forcedDir;
    a.forcedStep = forcedStep;
    std::printf("data dir: %s (rsc=%s)\n", dataDir.c_str(), a.vfs.hasRsc() ? "yes" : "no");

    a.window = SDL_CreateWindow("Vurus (native port, WIP)", SDL_WINDOWPOS_CENTERED,
                                SDL_WINDOWPOS_CENTERED, a.options.width, a.options.height,
                                SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!a.window || !a.gfx.init(a.window)) {
        std::fprintf(stderr, "GL init failed: %s\n", SDL_GetError());
        return 1;
    }

    if(!mute) {
        std::string error;
        if(!a.audio.device().configure({a.options.audioRate,a.options.audioBits,a.options.audioChannels},error))std::fprintf(stderr,"audio unavailable: %s; continuing silently\n",error.c_str());
        else {
            a.audio.device().setMasterVolume(volume);
            const char* driver=SDL_GetCurrentAudioDriver();
            std::fprintf(stderr,"audio: backend=%s, %d Hz, %d bit, %d channels, volume=%.0f%%, language=%s\n",
                driver?driver:"unknown",a.options.audioRate,a.options.audioBits,a.options.audioChannels,volume*100,audioLanguage.c_str());
            if(driver && (!std::strcmp(driver,"dummy") || !std::strcmp(driver,"disk")))
                std::fprintf(stderr,"audio: this test backend does not play through speakers; unset SDL_AUDIODRIVER for desktop playback\n");
        }
    }
#if defined(VYRUZ_ENABLE_SMAA) && !VYRUZ_ENABLE_SMAA
    // Imported options may enable a feature absent from this host build.
    // Disable only that feature for this run; keep bloom/sharpen and the file.
    if(a.options.smaa){a.options.smaa=false;std::fprintf(stderr,"SMAA unavailable in this platform build; disabled for this run\n");}
#endif
    bool displayFailure=false;
    if(!applyOptionsRuntime(a,a.options,optionsError,true,&displayFailure)){
        std::fprintf(stderr,"options apply: %s\n",optionsError.c_str());
        if(a.options.crt&&!a.gfx.crtEnabled())a.options.crt=false;
        if(!a.gfx.smaaEnabled())a.options.smaa=false;
        if(!a.gfx.sceneEffectsEnabled())a.options.fxaa=a.options.bloom=a.options.sharpen=false;
        if(displayFailure){
            std::fprintf(stderr,"using Windowed mode for this run\n");
            a.options.windowMode=0;std::string fallbackError;
            if(!applyOptionsRuntime(a,a.options,fallbackError))std::fprintf(stderr,"window fallback: %s\n",fallbackError.c_str());
        }
    }
    if (!(loadPath.empty()?loadLevelData(a,levelPath):loadSession(a))) {
        reportDataError("Incomplete or invalid game resources in "+dataDir+": "+a.loadError+". Restore resources from your original disc image.");
        a.audio.device().close();
        a.controller.close();
        a.gfx.shutdown();
        SDL_DestroyWindow(a.window);
        SDL_Quit();
        return 1;
    }
    a.audio.setMusicEnabled(!noMusic);
    if(!noMusic && a.audio.device().available()) {
        char track[32];std::snprintf(track,sizeof(track),"track%02d.wav",musicTrack);
        if(musicDir.empty()) {
            auto directory=std::filesystem::path(dataDir)/"music";
            const auto besideBinary=std::filesystem::path(executableDirectory)/"music";
            const auto required=musicTrackExplicit?track:"track02.wav";
            std::error_code ec;
            if(!std::filesystem::is_regular_file(directory/required,ec)) {
                ec.clear();
                if(std::filesystem::is_regular_file(besideBinary/required,ec))directory=besideBinary;
            }
            musicDir=directory.string();
        }
        const auto path=(std::filesystem::path(musicDir)/track).string();std::string error;
        if(!(musicTrackExplicit?a.audio.musicFile(path,error):a.audio.musicDirectory(musicDir,error))){
            a.audio.musicTrack(musicTheme);
            std::printf("music: %s; using native WAV theme %d\n",error.c_str(),musicTheme);
        }
        else std::printf("music: %s%s\n",musicTrackExplicit?path.c_str():musicDir.c_str(),musicTrackExplicit?" (fixed override)":" (native CD playlist)");
    }
    if (a.haveLevel) {
        float wx, wy;
        if (havePlayerOverride) {
            a.worldX = playerOverrideX;
            a.worldY = playerOverrideY;
            std::printf("player override: world (%.0f, %.0f) = cell (%.1f, %.1f)\n",
                        a.worldX, a.worldY, a.worldX / kTileW, a.worldY / kTileH);
        } else if (findPlayerStart(a, wx, wy)) {
            a.worldX = wx;
            a.worldY = wy;
            std::printf("player start from .3DO: world (%.0f, %.0f) = cell (%.1f, %.1f)\n",
                        wx, wy, wx / kTileW, wy / kTileH);
        } else {
            a.worldX = 320.f;
            a.worldY = 240.f;
        }
    } else {
        loadStoryTexture(a);
    }

    if(loadPath.empty()||havePlayerOverride)a.world.setPlayerPosition(a.worldX, a.worldY);
    bindEffectCamera(a);
    if (walkSteps > 0) {
        std::printf("walking %d steps dir (%.1f, %.1f) from (%.0f, %.0f)\n", walkSteps, walkDx,
                    walkDy, a.worldX, a.worldY);
        const int walkDir = vyruz::directionFromVector(walkDx, walkDy);
        if (walkDir < kPlayerDirections) { a.motion.direction = walkDir; a.world.setPlayerDirection(walkDir); }
        for (int i = 0; i < walkSteps; ++i) tryMove(a, walkDx, walkDy, 3.f);
        std::printf("player after walk: world (%.0f, %.0f) = cell (%.1f, %.1f), dir %d\n", a.worldX,
                    a.worldY, a.worldX / kTileW, a.worldY / kTileH, a.motion.direction);
    }

    advanceSceneAnimation(a,animationMs,a.world.elapsedMilliseconds()+animationMs);a.hud.advance(animationMs,&a.world);
    if (a.forcedDir >= 0) a.world.setPlayerDirection(a.forcedDir);
    if(probeMoney>=0) {auto carry=a.world.carry();carry.vitals.money=probeMoney;a.world.restore(carry);}
    if(probeMap)a.world.toggleMap();
    for (int step=0; step<simulateMs*60/1000; ++step) {
        bindEffectCamera(a);
        a.world.tick(kMoveDt, {{},simulateFire,0});
        std::string error;
        if (!a.sceneRenderer.sync(a.content,error)) { std::fprintf(stderr,"%s\n",error.c_str());a.renderFailed=true;break; }
        advanceSceneAnimation(a,kMoveDt*1000,a.world.elapsedMilliseconds());a.hud.advance(kMoveDt*1000,&a.world);
        a.audio.consume(a.world,a.content,a.sceneRenderer.takeAudioCues());
        for (auto [id,revision]:a.sceneRenderer.finishedGates(a.world.actors())) a.world.finishGateAnimation(id,revision);
        for (auto id:a.sceneRenderer.finishedEffects(a.world.actors())) a.world.expireVisual(id);
        serviceSave(a);
    }
    {
        const auto position=a.world.playerPosition();a.worldX=position.x;a.worldY=position.y;
        a.motion.direction=a.world.player().direction;
        std::printf("gameplay: shots=%llu destroyed=%llu pickups=%llu energy=%.0f shield=%.0f actors=%zu\n",
            static_cast<unsigned long long>(a.world.shotsFired),static_cast<unsigned long long>(a.world.actorsDestroyed),
            static_cast<unsigned long long>(a.world.pickupsTaken),a.world.player().vitals.energy,
            a.world.player().vitals.shield,a.world.actors().size());
    }
    a.world.damage(a.world.playerIndex(),probeDamage);a.paused=probePause;
    if((probePause||probeInventory) && a.world.mapOpen())a.world.toggleMap();
    if(probeInventory && a.world.player().alive)a.inventory.toggle(a.paused);
    if(!writePath.empty()) {
        const auto previous=a.savePath;const bool previousHistory=a.saveHistory;
        a.savePath=writePath;a.saveHistory=false;std::string error;
        if(!saveSession(a,0,error)){std::fprintf(stderr,"save failed: %s\n",error.c_str());a.renderFailed=true;}
        else std::printf("wrote debug checkpoint: %s\n",writePath.c_str());
        a.savePath=previous;
        a.saveHistory=previousHistory;
    }
    const bool startupMenu=loadPath.empty() && writePath.empty() && probeMenu.empty() && !directPlay && shotPath.empty() && !profileFrames && !simulateMs && !walkSteps &&
        !havePlayerOverride && forcedDir<0 && !probeInventory && !probeMap && !probePause;
    a.retryPersistence=shotPath.empty() && !profileFrames && !walkSteps && !simulateMs && writePath.empty();
    if(startupMenu || probeMenu=="main") {
        //The preloaded first map is title scenery, not a previously played session.
        a.restartLevel.reset();a.restartPath.clear();a.restartProfile.clear();
        recoverRetrySession(a);a.menu.retryAvailable=a.restartLevel.has_value();
    } else if(a.retryPersistence && loadPath.empty() && probeMenu.empty())persistRetrySession(a,true);
    if(startupMenu || !probeMenu.empty()) {
        if(a.inventory.open)a.inventory.close(a.paused);
        if(a.world.mapOpen())a.world.toggleMap();
        a.sessionStarted=!(startupMenu||probeMenu=="main");
        a.menu.show(a.paused,!a.sessionStarted);
        if(probeMenu=="controls"){a.menu.move(3);a.menu.activate(a.paused);}
        if(probeMenu=="confirm"){a.menu.move(2);a.menu.activate(a.paused);}
    }
    a.audio.device().setPaused(a.presentation.open()?a.presentation.suspended:(a.paused && a.sessionStarted));
    if(!a.sessionStarted)a.audio.device().stopEffects();
    if(startupMenu)beginStartup(a);
    const Uint64 timerFrequency = SDL_GetPerformanceFrequency();
    Uint64 lastFrame = SDL_GetPerformanceCounter();
    double pendingTime = 0.0;
    double softwareDeadline = static_cast<double>(lastFrame);
    int previousRenderHz=-1;
    std::string lastTitle;
    std::vector<double> frameTimes,tickTimes,renderTimes,workTimes,warmupTimes,worldTimes,animationTimes;
    int profileFrame=0,profileCatchups=0,profileDroppedTicks=0;
    if(profileFrames) {
        frameTimes.reserve(profileFrames);tickTimes.reserve(profileFrames*2);
        renderTimes.reserve(profileFrames);workTimes.reserve(profileFrames);
        warmupTimes.reserve(profileWarmup);
    }
    while (!a.quit && !a.renderFailed) {
        const Uint64 now = SDL_GetPerformanceCounter();
        const double menuFrameMs=1000.0*static_cast<double>(now-lastFrame)/timerFrequency;
        const bool measuring=profileFrames && profileFrame>=profileWarmup;
        a.profileTick=measuring;
        pendingTime += std::min(0.1, static_cast<double>(now - lastFrame) / timerFrequency);
        lastFrame = now;
        // Movement constants are per 1/60-second simulation step. Keep their
        // rate independent of the monitor, GL swap interval, or SDL_Delay.
        int catchupSteps=0;
        while (pendingTime >= kMoveDt && catchupSteps<4 && !a.quit && shotPath.empty()) {
            ++catchupSteps;
            const auto tickStart=SDL_GetPerformanceCounter();
            a.previousPositions.clear();
            for(const auto& object:a.content.objects)a.previousPositions.push_back(object.position);
            a.profileWorldMs=0;
            handleInput(a);
            if(measuring)worldTimes.push_back(a.profileWorldMs);
            advanceDeathPrompt(a,kMoveDt*1000.0);
            const bool liveDeath=a.presentation.page==vyruz::PresentationPage::DeathPrompt && !a.presentation.suspended;
            if(a.paused||(a.presentation.open()&&!liveDeath))a.previousPositions.clear();
            a.audio.device().setPaused(a.presentation.open()?a.presentation.suspended:(a.paused && a.sessionStarted));
            const auto animationStart=measuring?SDL_GetPerformanceCounter():0;
            if (liveDeath || (!a.presentation.open() && !a.paused && a.world.player().alive)) { advanceSceneAnimation(a,kMoveDt*1000,a.world.elapsedMilliseconds());a.hud.advance(kMoveDt*1000,&a.world); }
            if(measuring)animationTimes.push_back(1000.0*(SDL_GetPerformanceCounter()-animationStart)/timerFrequency);
            if(a.inventory.open)a.hud.advanceInventory(kMoveDt*1000,a.world,a.inventory);
            a.audio.setListenerHalfWidth(unsigned(std::max(1.f,a.gfx.viewport().width*.5f)));
            if(a.sessionStarted&&(!a.presentation.open()||liveDeath))a.audio.consume(a.world,a.content,a.sceneRenderer.takeAudioCues());
            if(!a.presentation.open()||liveDeath)a.audio.updateSpeech();
            a.audio.updateMusic();
            for (auto [id,revision]:a.sceneRenderer.finishedGates(a.world.actors())) a.world.finishGateAnimation(id,revision);
            for (auto id:a.sceneRenderer.finishedEffects(a.world.actors())) a.world.expireVisual(id);
            if(measuring)tickTimes.push_back(1000.0*(SDL_GetPerformanceCounter()-tickStart)/timerFrequency);
            pendingTime -= kMoveDt;
        }
        // Drop excess whole ticks after a stall instead of accumulating an
        // unbounded catch-up burst. Keep the fractional remainder.
        if(measuring && catchupSteps>1)++profileCatchups;
        if (pendingTime >= kMoveDt) {
            if(measuring)profileDroppedTicks+=static_cast<int>(pendingTime/kMoveDt);
            pendingTime=std::fmod(pendingTime,kMoveDt);
        }
        if (a.forcedDir >= 0)
            a.motion.direction = ((a.forcedDir % kPlayerDirections) + kPlayerDirections) % kPlayerDirections;
        std::string renderError;
        if (!a.sceneRenderer.sync(a.content, renderError)) {
            std::fprintf(stderr, "render update failed: %s\n", renderError.c_str());
            a.renderFailed = true; break;
        }
        bool levelChanged=applyMenuAction(a);
        if(!a.saveFuture.valid())levelChanged=serviceAttractDemo(a,SDL_GetTicks64())||levelChanged;
        serviceGameOver(a);
        const bool savedThisFrame=a.saveFuture.valid()?serviceSave(a,true):(!a.demoActive&&!a.presentation.open()&&serviceSave(a,true));
        if(a.saveCatalogFuture.valid())refreshSaveCatalog(a);
        if (!a.saveFuture.valid() && !a.demoActive && a.world.transition() && !a.menu.open() && !a.presentation.open()) {
            const auto request=*a.world.transition();
            levelChanged=changeLevel(a,request)||levelChanged;
        }
        if (a.restart && !a.demoActive && !a.saveFuture.valid()) {
            if(restartLevel(a))levelChanged=true;
            a.restart=false;
        }
        if (levelChanged || savedThisFrame) {
            a.previousPositions.clear();
            pendingTime=0;lastFrame=SDL_GetPerformanceCounter();softwareDeadline=static_cast<double>(lastFrame);
        }
        a.audio.device().setPaused(a.presentation.open()?a.presentation.suspended:(a.paused && a.sessionStarted));
        const auto& player = a.world.player();
        const auto* weapon = player.weapons.empty() ? nullptr : &player.weapons[player.selectedWeapon];
        const auto ammo = weapon ? vyruz::weaponStock(player,*weapon) : player.ammo.end();
        char title[512];
        std::snprintf(title, sizeof(title), "Vurus | %s | Energy %.0f Shield %.0f | %s %u | %s",
                      a.options.playerName.c_str(),player.vitals.energy, player.vitals.shield, weapon ? weapon->name.c_str() : "No weapon",
                      ammo != player.ammo.end() ? ammo->second.count : 0, a.world.message().c_str());
        if (lastTitle!=title) { SDL_SetWindowTitle(a.window,title);lastTitle=title; }
        const auto presentationNow=SDL_GetTicks64();
        if(a.presentation.open()) {
            const bool bip=a.presentation.page!=vyruz::PresentationPage::DeathPrompt && a.presentation.advance(double(presentationNow-a.presentationLastTick));
            if(bip&&a.presentation.page==vyruz::PresentationPage::Mission) {
                //428690: (1 << ((1 <<16)-2)) with x86 shift masking is bit30.
                //45f7e0 scans16channels: original requests admit no Bip sound.
                a.audio.briefingBip();
            }
            if(a.presentation.page==vyruz::PresentationPage::SlideShow && a.presentation.elapsedMilliseconds()>=a.slideDeadline){if(!nextSlide(a))finishPresentation(a);a.slideDeadline=a.presentation.elapsedMilliseconds()+4000;}
            const double movieClock=a.audio.device().playbackMilliseconds(a.presentationVoice).value_or(a.presentation.elapsedMilliseconds());
            if(a.presentation.timedOut() || (a.presentation.page==vyruz::PresentationPage::Movie && movieClock>=a.intro.durationMilliseconds()))finishPresentation(a);
        }
        a.presentationLastTick=presentationNow;
        if(!a.menu.open())a.hud.advanceMenu(vyruz::MenuPage::Closed,0,60);
        else if(!a.presentation.open()) {
            SDL_DisplayMode mode{};const int display=SDL_GetWindowDisplayIndex(a.window);
            const int menuRefresh=display>=0 && SDL_GetCurrentDisplayMode(display,&mode)==0?mode.refresh_rate:60;
            const unsigned flashes=a.hud.advanceMenu(a.menu.page,static_cast<uint32_t>(std::clamp(menuFrameMs,0.0,double(UINT32_MAX))),menuRefresh,a.menuPointerVisible);
            for(unsigned i=0;i<flashes;++i)a.audio.cue("electric.wav");
        }
        const auto renderStart=SDL_GetPerformanceCounter();
#if VYRUZ_GLES
        // EGL may discard/exchange the backbuffer on swap. --shot exits after
        // this frame, so read its final pixels before any swap occurs.
        a.gfx.setSwapEnabled(shotPath.empty());
#endif
        render(a,shotPath.empty()?static_cast<float>(pendingTime/kMoveDt):1.f);
        if(a.options.crt&&!a.gfx.crtEnabled()){a.options.crt=false;a.menu.error="CRT disabled: "+a.gfx.crtError();}
        if(a.options.smaa&&!a.gfx.smaaEnabled()){a.options.smaa=false;a.menu.error="SMAA unavailable; disabled for this run.";}
        if((a.options.fxaa||a.options.bloom||a.options.sharpen)&&!a.gfx.sceneEffectsEnabled()){a.options.fxaa=a.options.bloom=a.options.sharpen=false;a.menu.error="Scene effects unavailable; disabled for this run.";}
        if(a.presentation.dismissalReady())finishPresentation(a);
        if(measuring)renderTimes.push_back(1000.0*(SDL_GetPerformanceCounter()-renderStart)/timerFrequency);
        if (!shotPath.empty()) {
            if(!saveScreenshot(shotPath.c_str(), a.gfx.drawableWidth(), a.gfx.drawableHeight()))a.renderFailed=true;
            a.quit = 1;
        }
        if (a.showInfo && a.debugEnabled) {
            static int frame = 0;
            if (++frame % 120 == 0) {
                std::printf("[ESC menu][F1 info] world=(%.0f,%.0f) cell=(%.1f,%.1f)\n",
                            a.worldX, a.worldY, a.worldX / kTileW, a.worldY / kTileH);
            }
        }
        if(measuring)workTimes.push_back(1000.0*(SDL_GetPerformanceCounter()-now)/timerFrequency);
        // Cap presentation independently of the fixed 60 Hz simulation. Apply
        // even with VSync (e.g. 30 FPS on a 60 Hz display); swap wait already
        // counts toward the absolute deadline. Zero skips only software pacing.
        if(!renderHzOverride)renderHz=a.options.frameCap;
        if(renderHz!=previousRenderHz){softwareDeadline=static_cast<double>(now);previousRenderHz=renderHz;}
        if (renderHz>0 && !a.quit) {
            softwareDeadline+=static_cast<double>(timerFrequency)/renderHz;
            double counter=static_cast<double>(SDL_GetPerformanceCounter());
            // Absolute deadlines avoid adding render time to every frame.
            if(counter>softwareDeadline) softwareDeadline=counter;
            while(counter<softwareDeadline) {
                const double remainingMs=(softwareDeadline-counter)*1000/timerFrequency;
                if(remainingMs>=2) SDL_Delay(static_cast<Uint32>(remainingMs-1));
                // Yielding with SDL_Delay(0) repeatedly polls for the final
                // millisecond. Sleep that remainder so the CPU can idle.
                else std::this_thread::sleep_for(std::chrono::duration<double,std::milli>(remainingMs));
                counter=static_cast<double>(SDL_GetPerformanceCounter());
            }
        }
        if(renderHz==0)softwareDeadline=static_cast<double>(SDL_GetPerformanceCounter());
        recordDebugFrame(a,1000.0*(SDL_GetPerformanceCounter()-now)/timerFrequency);
        if(profileFrames) {
            const double elapsed=1000.0*(SDL_GetPerformanceCounter()-now)/timerFrequency;
            if(measuring)frameTimes.push_back(elapsed);else warmupTimes.push_back(elapsed);
            ++profileFrame;
            if(static_cast<int>(frameTimes.size())>=profileFrames)a.quit=1;
        }
    }
    if(profileFrames) {
        const auto report=[](const char* name,std::vector<double> samples) {
            if(samples.empty())return;
            std::sort(samples.begin(),samples.end());
            const auto percentile=[&](double p){return samples[std::min(samples.size()-1,static_cast<size_t>(samples.size()*p))];};
            std::printf("profile %s ms: p50=%.3f p95=%.3f p99=%.3f max=%.3f samples=%zu\n",
                name,percentile(.50),percentile(.95),percentile(.99),samples.back(),samples.size());
        };
        report("warmup frame (excluded)",warmupTimes);
        report("frame (including present/pacing)",frameTimes);
        report("simulation/input/animation/audio tick",tickTimes);
        report("World tick",worldTimes);report("scene/HUD animation tick",animationTimes);
        report("render/present",renderTimes);report("frame before software pacing",workTimes);
        std::printf("profile level=%s objects=%zu actors=%zu resolution=%dx%d\n",a.content.mapPath.c_str(),a.content.objects.size(),a.world.actors().size(),a.options.width,a.options.height);
        const double budget=renderHz>0?1000.0/renderHz:std::numeric_limits<double>::infinity();
        const auto overruns=std::count_if(frameTimes.begin(),frameTimes.end(),[&](double ms){return ms>budget*1.5;});
        std::printf("profile requested_hz=%d vsync=%d warmup=%zu frames_over_1.5_budget=%zu catchup_frames=%d dropped_ticks=%d\n",
                    renderHz,a.gfx.vsyncEnabled(),warmupTimes.size(),static_cast<size_t>(overruns),profileCatchups,profileDroppedTicks);
    }

    cancelSavePreview(a);
    if(a.saveFuture.valid()){a.saveFuture.wait();serviceSave(a,true);}
    a.audio.device().close();
    a.loading.clear(a.gfx);
    a.sceneRenderer.clear(a.gfx);
    a.hud.clear(a.gfx);
    a.controller.close();
    a.gfx.deleteTexture(a.savePreviewTexture);
    a.gfx.deleteTexture(a.extraTex);a.gfx.deleteTexture(a.storyTex);a.gfx.deleteTexture(a.movieTex);a.gfx.deleteTexture(a.endingTex);a.gfx.deleteTexture(a.endingFrameTex);
    a.gfx.shutdown();
    if (a.window) SDL_DestroyWindow(a.window);
    SDL_Quit();
    std::printf("\nbye\n");
    return a.renderFailed ? 1 : 0;
}
