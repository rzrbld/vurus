#pragma once
#include "render/texture_residency.h"
#include "render/sprite_sequences.h"
#include "render/animation.h"
#include "game/attached_fx_altitude.h"
#include "render/enemy_orbits.h"
#include "render/scene_state.h"
#include "render/player_turn.h"
#include "render/gfx.h"
#include "render/occlusion.h"
#include "game/content.h"
#include "game/world.h"
#include <map>

namespace vyruz {
//431ae0/433b30 enemy-only phase protocol. Translation is native surface
// world100c/1010 (normally -camera), dimensions are the virtual viewport.
struct EnemyAnimationView {
    int32_t translationX=0,translationY=0;
    uint32_t width=0,height=0;
    int mapW=0,mapH=0;
    const NativeActorCells* cells=nullptr;
    bool playerOnly=false;
    // Native world20&100: set by successful level graphics initialization,
    // cleared on game-loop exit; caller makes the session latch explicit.
    bool visibilityStep=false;
    // Explicit isolated oracle context; gameplay must supply linked cells.
    bool unboundedOracle=false;
};
enum class EnemyAnimationPhase { AwakeUpdate, VisibleUpdate, Draw };
enum class SceneAudioCueKind { Play,Held,Forget };
struct SceneAudioCue {
    size_t object=0; std::string bankPath; uint32_t sample=0;
    SceneAudioCueKind kind=SceneAudioCueKind::Play;
    // Transient ANM identity, independent of object/vector addresses and saves.
    uint64_t animation=0;
    std::optional<std::array<float,3>> source; // Compact scenery has no ActorState.
};
class SceneRenderer {
public:
    bool reactorFx=true, groundDetails=true, shadows=true;
    std::optional<ContentPosition> playerBodyCenter() const { return playerBodyCenter_; }
    std::vector<SceneAudioCue> takeAudioCues();
    SceneRendererState capture() const;
    std::optional<AnimationCursorState> itemCursor(size_t object,uint32_t sequence) const;
    bool restore(const SceneRendererState&,std::string& error);
    bool load(const SceneContent&, const ContentReader&, std::string& error);
    // Decode catalog sprites during the loading screen, before their first
    // projectile/death birth. Creates no channels, cursors, textures or sounds.
    void preloadClassSprites(const SceneContent&);
    bool loadTerrain(const NativeTileBodyCatalog&,std::string& error,NativeRandomState presentationRandom={});
    bool sync(const SceneContent&, std::string& error);
    void clear(Gfx&);
    // Discard reproducible GPU copies while retaining images and animation state.
    void releaseTextures(Gfx&);
    void advance(double milliseconds,const std::vector<ActorState>* actors=nullptr,double worldTimeMs=-1,const SurpriseState* surprise=nullptr);
    // Legacy advance remains an unbounded single-pass adapter for isolated art
    // fixtures. Gameplay enables this protocol and supplies every native phase.
    void setNativeEnemyPhases(bool enabled=true) { nativeEnemyPhases_=enabled; }
    bool nativeEnemyPhases() const { return nativeEnemyPhases_; }
    void advanceEnemyPhase(double milliseconds,const std::vector<ActorState>&,
        const EnemyAnimationView&,EnemyAnimationPhase,std::optional<size_t> awakeActor={});
    void syncActors(const std::vector<ActorState>&);
    std::vector<std::pair<size_t,uint64_t>> finishedGates(const std::vector<ActorState>&) const;
    std::vector<size_t> finishedEffects(const std::vector<ActorState>&) const;
    void draw(Gfx&, const SceneContent&, const Level&, float cameraX, float cameraY,
              float playerX, float playerY, int direction, int forcedBitmap,
              const std::function<void(const ForegroundTile&)>& drawTile,
              const std::vector<ActorState>* actors=nullptr,
              const std::vector<ContentPosition>* previous=nullptr,float alpha=1,const SurpriseState* surprise=nullptr,bool playerOnly=false);
private:
    SpriteSequenceUses sequenceUses_;
    bool decodeBank(const std::string&,GafAnimation&);
    PlayerTurnPresentation playerTurn_;
    std::optional<size_t> playerObject_;
    std::optional<ContentPosition> playerBodyCenter_;
    TextureResidency textureResidency_;
    struct Bank { GafAnimation animation; std::vector<GLuint> textures; };
    struct Channel { size_t object; bool shadow; Bank* bank; uint32_t sequence; AnimationCursor cursor; uint64_t gateRevision=0; bool firing=false; uint64_t fireRevision=0; bool lift=false; std::vector<GafSubframe> pendingAudio{}; bool spider=false; uint32_t baseSequence=0; uint64_t visualRevision=0; Bank* generatorBank=nullptr; bool derivedInvisible=false; float visibility=0,fadeClockMs=0; bool visibilityAlpha=true; bool helicopter=false; bool walker=false; bool walkerTurned=false; int flightTilt=0; double flightClockMs=0; ContentPosition flightPosition{},flightPrevious{};
        // Derived pose policy: -1 timed, 0 fixed, 1/2 entries per heading.
        bool skipNewbornAdvance=false;
        int shadowPoseStride=-1;
        bool turretBase=false; // Native primary-bank sequence25, ground anchor.
        bool bankedEnemy=false,tank=false;
        bool nativeEnemy=false,nativeGraphicUpdate=false,nativeInGround=false,nativeDetail=false;
        bool enemyDrawAdmitted=false;
        int enemyLean=0,leanSubtype=0; // Renderer4333f0 state, independent of gameplay.
        uint64_t audioIdentity=0;
    };
    bool nativeEnemyPhases_=false;
    std::vector<std::vector<size_t>> enemyObjectChannels_;
    void rebuildEnemyObjectChannels();
    void syncActorChannel(Channel&,const ActorState&);
    void advanceEnemyChannels(size_t,double,const std::vector<ActorState>&,EnemyAnimationPhase,bool visibilityStep);
    void queueAudio(Channel&,const GafSubframe&);
    void holdAudio(const Channel&);
    void forgetAudio(Channel&);
    uint64_t nextAudioIdentity_=1;
    size_t queuedAudioStarts_=0;
    std::vector<SceneAudioCue> audioCues_;
    std::map<std::string,Bank> banks_;
    struct TerrainChannel { NativeTileBodyPlacement placement; Bank* bank=nullptr;
        uint32_t sequence=0,subtype=0,startMs=0; AnimationCursor cursor;
        uint64_t audioIdentity=0;
    };
    std::vector<TerrainChannel> terrain_;
    std::vector<std::array<size_t,4>> terrainCells_;
    uint32_t terrainWidth_=0,terrainHeight_=0;
    double terrainClockMs_=0;
    void advanceTerrain(double,const EnemyAnimationView&);
    std::vector<Channel> channels_;
    // Compact traversal metadata keeps expired FX history out of hot cursor
    // scans without changing channel indices or persisted animation state.
    struct ChannelScan { size_t object; uint64_t visualRevision; bool passive,helicopter,inactive=false; };
    std::vector<ChannelScan> channelScan_;
    std::vector<size_t> workingChannels_;
    void refreshChannelScan(size_t first=0);
    // Enemy firing is derived presentation state, excluded from old save layouts.
    std::vector<Channel> enemyFire_;
    bool resetEnemyFire_=false;
    ContentReader read_;
    AttachedFxAltitude orbitAltitudes_[9];
    SpriteReference orbitSprites_[9]; // Banana, five Invulnerability and three Touch groups
    struct AttachedOrbit { size_t object; EnemyOrbitConfig config; EnemyOrbitState state; float visibility=0,fadeClockMs=0; bool visible=false; AnimationCursor cursor{}; bool animationInitialized=false; ContentPosition parent{}; float altitude=0; };
    std::vector<AttachedOrbit> enemyOrbits_;
    double orbitTimeMs_=0;
    size_t objectCount_=0;
};
}
