#pragma once
#include "game/effect_camera_native.h"
#include "game/indirect_optional.h"
#include "game/demo_replay.h"
#include "game/enemy_turn_native.h"
#include "game/native_random.h"
#include "game/native_awake_state.h"
#include "game/player_facing_native.h"
#include "game/native_actor_cells.h"
#include "game/native_projection_cache.h"
#include "game/navigation_item_cache.h"
#include "game/class_name_index.h"
#include "game/generator_occupancy_native.h"
#include "game/native_ammo_cells.h"
#include "game/world_pool_state.h"
#include "game/tile_body_native.h"
#include "game/ammo_owner_counters_native.h"
#include "game/ordinary_enemy_ai_native.h"
#include "game/grenade_contact_native.h"
#include "game/native_body_contacts.h"
#include "game/brain.h"
#include "game/final_object.h"
#include "game/ordered_inventory.h"
#include "game/difficulty.h"
#include "game/audio_events.h"
#include "game/map_view.h"
#include "game/notifications.h"
#include "game/combat.h"
#include "game/controls.h"
#include "game/motion_contact.h"
#include "game/enemy_motion.h"
#include "game/interactions.h"
#include "game/level.h"
#include "game/navigation.h"
#include "game/spider_ai_state.h"
#include "game/missile_guidance.h"
#include "game/gates.h"
#include "game/lifts.h"
#include "game/surprise.h"
#include "render/animation.h"
#include <random>
#include <map>
#include <set>
#include <unordered_map>

namespace vyruz {
struct EquippedWeapon {
    std::string name, ammo;
    WeaponConfig config;
    WeaponState state;
    float range=0;
    bool visibleGunFire=false;
    std::optional<size_t> ammoSlot;
};
struct PendingExplosion {
    float damage=0, radius=0, remainingMs=500, altitude=0;
    std::optional<size_t> creator;
    AnimationCursorState animation;
    bool initialized=false;
};
struct FxTrailSegment { ContentPosition previous;float step=0; };
struct ActorCreate {
    ClassId classId{};
    ContentPosition offset;
    float nextMs=0,repeatMs=0,trailStep=0;
    std::optional<ContentPosition> previousOrigin;
    int direction=0;
    bool done=false,currentPosition=false;
};
struct GeneratorState {
    std::optional<ClassId> product;
    int maxBirths=0,birthCount=0;
    float delayMs=0,durationMs=0,remainingMs=0;
    bool stressable=false,active=false,stablePopulation=false;
};
struct ActorState {
    std::uint32_t nativeSubtype=0; // Placement-derived, not serialized; rebuilt on restore.
    bool nativeAwake=false,nativeManaged=false;
    bool nativeFireRequested=false; // actor flag2, retained through Two-pieces conversion.
    bool hasAmmoAttribute=false; // Resolved instance role; derived on construction/restore, not serialized.
    std::size_t nativeAwakeIndex=0;
    std::uint32_t nativeGeneration=0,nativeCreatorGeneration=0;
    NativeAmmoOwnerCounters nativeAmmoOwner;
    IndirectOptional<OrdinaryEnemyNativeState> ordinaryAI;
    OrdinaryEnemyNativeConfig ordinaryAIConfig; // Derived; rebuilt on restore.
    std::optional<EnemyTurnState> enemyTurn;
    EnemyTurnConfig enemyTurnConfig; // Class-derived; rebuilt on restore.
    int weaponHeading() const {
        if(spiderAI && spiderAI->turn)return spiderAI->turn->logicalHeading;
        return enemyTurn && enemyTurnConfig.twoPieces
            ?enemyTurn->gunLogicalHeading:direction;
    }
    // Native X/Z momentum. Player momentum remains in PlayerMotion.
    ContactVelocity contactVelocity;
    bool contactMotion=false;
    float enemyThrust=0,enemyMagnitude=0;
    bool spiderPatrolTurning=false; // retired save23 field
    IndirectOptional<SpiderAIState> spiderAI;
    // Shared native wall-contact coordinates; Spider v25 history stays in its AI.
    std::optional<SpiderMotionHistory> nativeMotion;
    std::optional<NativeGeneratorSquareState> nativeGeneratorSquare;
    SpiderMotionHistory* nativeMotionHistory() {
        if(spiderAI && spiderAI->motion)return &*spiderAI->motion;
        return nativeMotion?&*nativeMotion:nullptr;
    }
    const SpiderMotionHistory* nativeMotionHistory() const {
        if(spiderAI && spiderAI->motion)return &*spiderAI->motion;
        return nativeMotion?&*nativeMotion:nullptr;
    }
    int enemyMoveCommand=0; // native 0 / forward200 / reverse208
    bool enemyMovedLastStep=false;
    double contactResponseMs=-1; // per-tick response guard, not checkpoint state
    bool enemyMotionInitialized=false;
    bool nativeCameraGated=false; // Derived subtype rule440cf6; rebuilt on restore.
    std::optional<ContentPosition> driveSegmentStart,driveSegmentEnd;
    EnemyMotionConfig enemyMotionConfig; // authored, rebuilt on restore
    struct ContactConfig {
        float weight=1,elasticity=1,grip=1,frictionX=.97f,frictionZ=.97f;
        float thresholdX=.01f,thresholdZ=.01f;
        bool inertia=true,enabled=false,item=false,launchedThing=false,nativeEnemy=false;
    } contactConfig; // authored, rebuilt on load/restore
    TimedProtectionState timedProtection;
    bool directFireOnly=false,finalObject=false; // class-derived, rebuilt on restore
    std::vector<ActorCreate> creates;
    bool createsInitialized=false;
    bool pooledTrail=false;
    bool reusedRecord=false; // Transient interpolation guard, like a newly appended body.
    std::size_t nativePoolSlot=SIZE_MAX;
    bool nativeHeldOriginal=false; //Derived local inventory alias; never spatial while held.
    // Low32: visual reset revision. High32: runtime birth order +1 (zero
    // preserves legacy slot order). This opaque saved token also preserves
    // completion order when a physical record is recycled.
    uint64_t visualRevision=0;
    std::vector<GeneratorState> generators;
    std::optional<size_t> generatorParent;
    bool generatorsInitialized=false;
    bool alive=true, pickup=false, solid=false, indestructible=false, enemy=false;
    bool directional=false, reached=false, effect=false, hudEnemy=false;
    bool turretOnlyBase=false; // Turret/Tank retained lower piece, derived on restore.
    // Ammo health and credit occur before the later actor death visit. These
    // adapters distinguish a damaged pending corpse from zero-Energy FX.
    bool nativeDamagePending=false,nativeKillCounted=false;
    bool nativeHit=false; // actor+128 bit200: persists until player render/death reset.
    bool nativeAmmoAbort=false; // transient actor-tail/CREATE admission
    // An impact can leave surface ahead of retained bounds. Survives rest/save
    //until the next movement commits, without changing ordinary player motion.
    bool nativeAmmoContinuation=false;
    int direction=0;
    uint64_t fireRevision=0;
    // Presentation-only timers. They never feed the accepted movement solver.
    // These transient effects restart on checkpoint restore; save layout unchanged.
    float visualThrust=0, engineGlowMs=0, hitFlashMs=0, shieldPulse=0;
    bool headlightVisible=true;
    // Derived each simulation tick; presentation only, not persisted.
    bool locomoting=false;
    bool walkingCommand=false; // Walker command200, including blocked attempts; transient
    // 42caa0/42d2d0: reveal throughout eligible attack, including cooldown.
    bool enemyRevealed=false;
    bool ammoHidden=false; // derived from saved projectile activation phase
    //4391d0(actor,0) disables bounds while keeping the body visible. Separate
    //from the later hidden Keep-bb phase; derived, not serialized.
    bool ammoContactDisabled=false;
    // Native Ammo skips the invisible draw gate and clamps operation3 alpha
    // to .25. These counters are presentation-only (14ms adapter like Drone).
    float ammoVisibility=49,ammoFadeClockMs=0;
    bool ammoAlpha=false;
    bool contactMine=false; // derived zero-speed, zero-direct-damage Ammo
    // Projectile body altitude derived from saved age/magnitude and class
    // Bounce-period; shadows stay at ground level. Not serialized.
    std::optional<float> projectileAltitude;
    std::optional<PendingExplosion> explosion;
    std::optional<FxTrailSegment> trailSegment;
    float radius=0, speed=0, turnMs=0, lifeMs=-1;
    VitalStats vitals;
    // Native actor+e4 is mutable (difficulty, Surprise and upper-piece death).
    // Absent in pre-v30 saves; reconstructed once on restore, never per tick.
    std::optional<float> shieldUpStep;
    bool visibleWhenFiring=false; // Resolved placement flag; rebuilt on construction/restore.
    bool shieldRegenerates=false; // class-derived subtype admission
    PickupEffect pickupEffect;
    BrainConfig brain;
    BrainState brainState;
    std::vector<EquippedWeapon> weapons;
    OrderedInventory<AmmoStock> ammo;
    size_t selectedWeapon=0;
    std::set<int> keys;
    std::vector<KeyPiece> heldKeys;
    double ageMs=0;
    float pickupDelayMs=0;
    float reactivateMs=-1;
    std::optional<float> itemTimeoutMs;
    std::vector<ContentPosition> route;
    size_t routePoint=0;
    std::optional<NavigationSearch> routeSearch;
    std::optional<ContentPosition> routeGoal,randomGoal;
    float routeRetryMs=0,randomWaitMs=0;
    std::optional<LiftConfig> lift;
    LiftState liftState;
    std::optional<GateConfig> gate;
    GateState gateState;
    uint64_t gateRevision=0;
    bool gateAnimationFinished=false;
};
inline uint64_t actorRecordOrder(const ActorState& actor,size_t index) {
    const auto birth=actor.visualRevision>>32;return birth?birth-1:index;
}
struct HeldItem {
    ObjectPlacement object;
    ActorState actor;
    std::string mapPath,className;
    size_t holder=0;
    bool surprise=false;
};
struct DeathDropScatter;
// Selected stock identity is separate from the compatible ammunition class.
inline auto weaponStock(ActorState& actor,const EquippedWeapon& weapon) {
    if(weapon.ammoSlot && *weapon.ammoSlot<actor.ammo.size() && actor.ammo.slots[*weapon.ammoSlot].first==weapon.ammo)
        return actor.ammo.begin()+*weapon.ammoSlot;
    return actor.ammo.find(weapon.ammo);
}
inline auto weaponStock(const ActorState& actor,const EquippedWeapon& weapon) {
    if(weapon.ammoSlot && *weapon.ammoSlot<actor.ammo.size() && actor.ammo.slots[*weapon.ammoSlot].first==weapon.ammo)
        return actor.ammo.begin()+*weapon.ammoSlot;
    return actor.ammo.find(weapon.ammo);
}
struct ProjectileCreate {
    ClassId classId{};
    ContentPosition offset;
    float nextMs=0,repeatMs=0,trailStep=0;
    std::optional<ContentPosition> previousOrigin;
    bool done=false,currentPosition=false;
};
struct NativeBareAmmoBody {
    bool awake=false;
    std::size_t awakeIndex=0;
    float energy=0,shield=0;
    bool damagePending=false,killCounted=false;
    bool nativeHit=false; // compact kind3 Ammo has the same persistent latch.
    SpiderMotionHistory motion;
};
struct Projectile {
    std::uint32_t nativeGeneration=0,nativeOwnerGeneration=0;
    std::uint64_t nativeBodyIdentity=0;
    std::size_t nativePoolSlot=SIZE_MAX;
    std::optional<NativeBareAmmoBody> nativeBareBody;
    size_t spiderGraphNode=SIZE_MAX;
    // No target class means launch acquisition failed: remain Straight forever.
    std::optional<ClassId> guidanceClass;
    std::optional<size_t> guidanceTarget;
    MissileGuidanceState guidance;
    std::vector<ContentPosition> guidanceRoute;
    size_t guidancePoint=0;
    ContentPosition guidanceStart{};
    std::optional<NavigationSearch> guidanceSearch;
    // v27 native ordered graph action. guidanceSearch is legacy v19–26 only.
    std::optional<MissileNativeNavigation> guidanceNative;
    ContentPosition position, velocity;
    ProjectileMotionConfig motionConfig;
    ProjectileMotionState motion;
    std::vector<ProjectileCreate> creates;
    float directionX=1,directionZ=0;
    size_t owner=0;
    std::optional<size_t> visual;
    ClassId classId{};
    float radius=1, damage=0, damageRadius=1, distance=0, range=0, ageMs=0;
    float lifetimeMs=-1, activeAfterMs=0, fuseMs=0;
    bool ignoreOwner=false, alive=true;
    bool slopeEnabled=false;
    float slopeRadians=0,slopeHeight=0,slopeAltitude=0;
    float inactiveMs=0,onMs=0,damageRemainingMs=0;
    bool phaseInitialized=false,phaseHidden=false;
    // Class-derived activation settings, not part of the checkpoint payload.
    bool phaseConfigReady=false,keepBoundsWhenHidden=false;
    float onInitial=0,offInitial=0,onMinimum=0,onMaximum=0,offMinimum=0,offMaximum=0;
    // Cached class-derived inputs, rebuilt on restore and not serialized.
    bool verticalBounce=false;
    float bouncePeriod=0;
    bool terrainBounce=false;
    float bounceElasticity=0;
    std::string hitEffect;
};
struct WorldInput { MovementCommand movement; bool fire=false; int weaponDelta=0; std::optional<DemoRecord> replayPose={}; };
struct PlayerCarry {
    VitalStats vitals;
    OrderedInventory<AmmoStock> ammo;
    std::set<int> keys;
    std::vector<KeyPiece> heldKeys;
    std::string weapon;
    uint64_t kills=0;
    double elapsedMs=0;
    std::optional<size_t> ammoSlot;
    std::vector<HeldItem> heldItems;
    uint64_t ammoHits=0,ammoMisses=0;
};
struct TransitionRequest { size_t lift=0; std::string path; int64_t cost=0; };
struct SaveRequest { size_t plot=0; int64_t cost=0; };
struct WorldSnapshot;

class GameWorld {
public:
    // Original context+2754 profile, separate from the vehicle's class Name.
    // Settings own this value; level/checkpoint restoration must not replace it.
    size_t retaliationTarget() const {
        return retaliationTarget_<actors_.size() && actors_[retaliationTarget_].alive &&
            actors_[retaliationTarget_].nativeGeneration==retaliationGeneration_
            ? retaliationTarget_ : SIZE_MAX;
    }
    void setPlayerName(const std::string& name) { playerName_=name; }
    // Application view/mode context, deliberately outside gameplay saves.
    // Headless callers may leave it unset for camera-independent mechanics.
    void setEffectCamera(NativeEffectCamera camera) { effectCamera_=camera; }
    void setEffectReader(ContentReader reader) { effectReader_=std::move(reader);effectSequences_.clear(); }
    bool load(SceneContent&,const Level&,std::string& error,int difficulty=1);
    const SurpriseState& surprise() const { return surprise_; }
    void cancelSurprise();
    int difficulty() const { return difficulty_.level; }
    NativeRandomState presentationRandomState() const { return nativeGlobalRandom_; }
    WorldSnapshot snapshot() const;
    bool restoreSnapshot(const WorldSnapshot&,std::string& error);
    void setNativeTileCatalog(NativeTileBodyCatalog catalog){nativeTileCatalog_=std::move(catalog);}
    const NativeTileBodyCatalog* nativeTileCatalog() const { return nativeTileCatalog_?&*nativeTileCatalog_:nullptr; }
    NativeWorldPoolState captureNativePool() const;
    NativeWorldPoolState nativePoolForReplacement() const;
    NativePoolReplacementContext nativePoolReplacementContext() const;
    // Exact continuation is the default WorldSnapshot API. Application-level
    // live replacement explicitly reconstructs identities in the continuing
    // session pool after its private load/validation has succeeded.
    bool restoreNativePool(const std::optional<NativeWorldPoolState>&,std::string& error);
    bool reconstructNativePoolForReplacement(const NativeWorldPoolState&,std::string& error,bool checkpoint=false);
    bool reconstructNativePoolForReplacement(const NativePoolReplacementContext&,std::string& error,bool checkpoint=false);
    void rebind(SceneContent& scene,const Level& level) { scene_=&scene;level_=&level;navigationItemMetadata_.clear();classNames_.clear(); }
    const std::optional<SaveRequest>& saveRequest() const { return saveRequest_; }
    void finishSave(bool success,const std::string& error={});
    void tick(float seconds,const WorldInput&);
    void toggleMap();
    void setHudLanguage(int language) { notifications_.setLanguage(language); }
    bool mapOpen() const { return map_.open(); }
    // Native world+20 bit200000: rebuilt by each manager pass, including
    // Time Warp's player-only pass; independent of lingering warning text.
    bool playerLocked() const { return playerLocked_; }
    const Notifications& notifications() const { return notifications_; }
    void announceLevel() { notifications_.levelName(scene_->levelName); }
    float playerSurfaceSpeed() const;
    float playerNativeSpeed() const { return motion_.magnitude; }
    float playerNativeThrust() const { return motion_.thrust; }
    bool audioMotorAttempt() const { return audioMotorAttempt_; }
    void movePlayer(float dx,float dy);
    void setPlayerPosition(float x,float y);
    void setPlayerDirection(int direction);
    bool selectPlayerWeaponSlot(unsigned slot); // zero-based native ADD Weapon order
    // Port-only testing controls: transient, deliberately excluded from saves.
    bool debugEnemyAI() const { return debugEnemyAI_; }
    bool debugGodMode() const { return debugGodMode_; }
    void setDebugEnemyAI(bool enabled);
    void setDebugGodMode(bool enabled) { debugGodMode_=enabled; }
    void debugFillAmmo();
    bool debugAddMoney();
    bool debugSetSurprise(SurpriseKind);
    void damage(size_t actor,float amount,std::optional<size_t> source=std::nullopt,bool radial=false,bool ammoSource=false);
    size_t enemiesRemaining() const;
    uint64_t playerKills() const { return playerKills_; }
    uint64_t ammoHits() const { return ammoHits_; }
    uint64_t ammoMisses() const { return ammoMisses_; }
    double elapsedMilliseconds() const { return elapsedMs_; }
    const NativeActorCells& nativeActorCells() const { return actorCells_; }
    size_t spawn(ClassId,ContentPosition);
    void expireVisual(size_t id);
    void initializeGates();
    void updateGates(float milliseconds);
    void finishGateAnimation(size_t actor,uint64_t revision);
    std::vector<AudioEvent> takeAudioEvents() { auto result=std::move(audioEvents_);audioEvents_.clear();return result; }
    // Ordinary player-input visits only. A repeated Circular Fire visit has
    // no such native call. Transient presentation signal, never saved.
    std::optional<bool> takeAudioInput() { auto result=audioInput_;audioInput_.reset();return result; }
    const std::vector<ActorState>& actors() const { return actors_; }
    const std::vector<Projectile>& projectiles() const { return projectiles_; }
    size_t playerIndex() const { return player_; }
    const ActorState& player() const { return actors_.at(player_); }
    void consumePlayerHitForRender(bool animationOnly=false,int pass=0);
    ContentPosition playerPosition() const { return scene_->objects[player_].position; }
    const std::optional<TransitionRequest>& transition() const { return transition_; }
    void rejectTransition(const std::string& reason);
    void finishEnding();
    double endingRemainingMilliseconds() const { return endingRemainingMs_; }
    PlayerCarry carry() const;
    void restore(const PlayerCarry&);
    const std::vector<std::string>& diagnostics() const { return diagnostics_; }
    const std::string& message() const { return message_; }
    uint64_t shotsFired=0, pickupsTaken=0, actorsDestroyed=0;
private:
    friend struct AmmoImpactNativeTestAccess;
    friend struct NavigationItemMetadataTestAccess;
    friend struct ClassNameIndexTestAccess;
    NativeAwakeManager awakeManager_;
    std::optional<std::size_t> awakeVisitIndex_;
    bool constructingScene_=false;
    void enrollNativeActor(std::size_t);
    void enrollNativeAmmo(Projectile&);
    void wakeNativeActor(std::size_t);
    void releaseNativeAwakeActor(std::size_t);
    void releaseNativeAwakeAmmo(Projectile&);
    void sleepNativeActor(std::size_t);
    NativeAwakeRestDecision nativeActorRestDecision(std::size_t,ContactVelocity) const;
    NativeAwakeState captureNativeAwake() const;
    void restoreNativeAwake(const std::optional<NativeAwakeState>&);
    bool advanceActorAI(std::size_t,float seconds);
    void visitNativeActor(std::size_t,float seconds,const WorldInput&,bool& locked);
    std::unordered_map<std::size_t,std::size_t> visualAmmoIndex_;
    struct PendingActorFire {
        std::size_t actor=0;bool held=false,sweep=false;
        std::optional<ContentPosition> previous;
        std::optional<bool> aiming;
    };
    bool deferActorFire_=false;
    void selectCompatibleActorAmmo(std::size_t);
    std::optional<PendingActorFire> deferredActorFire_;
    NativePlayerFacingState nativePlayerFacing_;
    ContentPosition nativePlayerPreviousSurface_;
    void updateGate(std::size_t,float milliseconds);
    std::optional<NativeEffectCamera> effectCamera_;
    bool effectPositionAdmitted(ContentPosition) const;
    bool actorUpdateAdmitted(size_t actor) const;
    bool debugEnemyAI_=true,debugGodMode_=false;
    PlayerMotionConfig playerMotionConfig_;
    void configurePlayerMotion();
    void configureWeaponSlopes(ActorState& actor);
    void configureEnemyTurning(ActorState&,const ObjectPlacement&,bool legacy=false);
    void configureShieldRegeneration(ActorState&,const ObjectPlacement&,const DifficultyPolicy&,bool player,bool legacy=false);
    void advanceShieldRegeneration(size_t actor);
    DifficultyPolicy difficulty_;
    Notifications notifications_;
    bool playerLocked_=false; // transient HUD warning, not checkpoint state
    void audioEvent(AudioEventKind kind,size_t actor,std::string name={}) {
        if(audioEvents_.size()<4096)audioEvents_.push_back({kind,actor,scene_->objects[actor].position,std::move(name)});
    }
    std::vector<AudioEvent> audioEvents_;
    std::optional<bool> audioInput_;
    bool playerMoveCommanded_=false; // Transient native forward/reverse input for contact admission.
    bool audioMotorAttempt_=false; // Transient input, never serialized.
    void radialInjuryCommentary(float amount);
    void voiceEvent(std::string name,unsigned delay=0,unsigned hold=1000,unsigned tail=0) {
        if(audioEvents_.size()<4096)audioEvents_.push_back({AudioEventKind::Voice,player_,playerPosition(),std::move(name),delay,hold,tail});
    }
    ActorState makeActor(const ObjectPlacement&);
    bool advanceLaunchedThing(size_t,float seconds);
    void configureActorCreates(ActorState&,const ObjectPlacement&,bool legacy=false);
    void updateActorCreates(size_t,ContentPosition previous,bool constructing=false);
    unsigned actorCreateDepth_=0;
    std::set<uint32_t> diagnosedActorCreates_;
    TerrainDisplacement move(size_t actor,float dx,float dy,bool physical=false,const ContactVelocity* nativeStep=nullptr);
    void configureContact(ActorState& actor,const ObjectPlacement& object);
    ContactVelocity contactVelocity(size_t actor) const;
    ContactVelocity effectiveContactVelocity(size_t actor) const;
    void setContactVelocity(size_t actor,ContactVelocity velocity);
    bool advanceContactMotion(size_t actor,float seconds);
    void applyAmmoImpulse(size_t actor,int heading);
    void contactAmmo(size_t actor,ContentPosition from,ContentPosition to);
    void resolveAmmoContacts();
    void recordAmmoContact(std::optional<size_t> owner,std::optional<size_t> target);
    bool updateAmmoActivation(Projectile&,float milliseconds);
    void initializeProjectileGuidance(Projectile&,size_t shooter);
    void updateProjectileGuidance(Projectile&,float milliseconds);
    bool ammoContactEnabled(const Projectile&) const;
    std::vector<std::pair<size_t,size_t>> ammoContactVictims_; // mine, incoming actor
    std::vector<size_t> contactMines_;
    void fire(size_t actor,bool held,bool sweep=false,std::optional<ContentPosition> forwardPrevious={},std::optional<bool> aiming={});
    bool takeSurprise(SurpriseKind);
    void updateSurprise(float milliseconds);
    void spawnBananaTrails(bool constructing);
    void updateSurpriseOrbitPositions();
    void initializeSurpriseOrbitAltitude();
    void applySurprise();
    void refreshSelectedAmmo();
    void selectLoadedPlayerWeapon();
    Displacement surpriseMovement(const MovementCommand&,float seconds);
    SurpriseState surprise_;
    size_t retaliationTarget_=SIZE_MAX;
    uint32_t retaliationGeneration_=0;
    std::string playerName_="Vyruz";
    std::vector<size_t> touchVictims_;
    void collect();
    HeldCode retainCode(size_t object) const;
    void retainItem(size_t object,bool surprise);
    void migrateHeldItems();
    bool consumeHeldKey(size_t holder,int key);
    void notifyAccessBlocked(size_t object);
    void releaseHeldItems(size_t holder,DeathDropScatter&);
    std::optional<size_t> materializeHeldItem(const HeldItem&,ContentPosition);
    void migrateHeldCodes();
    void updateHeldCodes(float milliseconds);
    void updateProjectile(size_t index,float seconds,bool nativeRouting);
    void updateProjectiles(float seconds);
    GrenadeNativeContactResult updateNativeGrenadeContacts(Projectile&,ProjectileDisplacement,float seconds);
    NativeAmmoContactObject nativeBodyContactObject(size_t actor) const;
    std::optional<NativeAmmoShapeTarget> resolveNativeBodyContact(uint64_t reference) const;
    NativeBodyContactQuery queryNativeBodyContacts(size_t sourceActor,ContactVelocity previous,
        ContactVelocity proposed,float radius);
    bool applyNativeActorAmmoContact(size_t incoming,const NativeBodyContactQuery&,float tickMilliseconds,
                                    ContactVelocity current,ContactVelocity proposed,ContactVelocity step,float factor);
    size_t spawnTrail(ClassId,ContentPosition);
    bool reusableTrail(ClassId) const;
    std::map<uint32_t,std::set<size_t>> freeTrailSlots_;
    std::map<uint32_t,std::vector<size_t>> freeTransientRecords_;
    // Conservative, sorted slot IDs; never a cached free/pinned decision.
    std::vector<size_t> transientRecordCandidates_;
    // Slots that can own actor references. Conservative membership survives
    // death/reuse; the collector still reads current references and admission.
    // Owned by World so move/rebind and checkpoint replacement retain it.
    std::vector<size_t> referenceOwnerCandidates_;
    // Nonpersistent authored query metadata; retained property identity handles
    // COW edits/recycled slots. Load, accepted restore and rebind discard it.
    std::vector<NavigationItemCache> navigationItemMetadata_;
    bool navigationItemOnlyForPlayer(size_t actor);
    // Catalog names are authored and immutable while bound. Rebind after
    // replacing a catalog; property edits are still read from current classes.
    ClassNameIndex classNames_;
    const ObjectClass* classByName(std::string_view name) { return classNames_.find(scene_->classes,name); }
    void registerReferenceOwnerCandidate(size_t);
    void registerTransientRecordCandidate(size_t);
    void rebuildTransientRecordCandidates();
    void collectReleasedTransientRecords();
    uint64_t nextRecordOrder_=0;
    void configureProjectileCreates(Projectile&,bool fromConstructor=false);
    void updateProjectileCreates(Projectile&,ContentPosition previous);
    bool spawnProjectileHitEffect(const Projectile&,bool contact);
    void applyAmmoImpact(Projectile&,std::optional<size_t> target,bool contact,NativeNavPoint radialCenter,float tickMilliseconds);
    void applyNativeAmmoDamage(size_t target,float amount,Projectile&,bool radial,bool radialPlayerOwned=false,
                              bool sourceIsAmmo=true);
    uint32_t nativeDamageFlags(size_t actor) const;
    uint32_t nativeDamageClassFlags(size_t actor) const;
    uint32_t nativeDamageSubtype(size_t actor) const;
    void initializeActorCells();
    void synchronizeActorCell(size_t actor);
    NativeActorCells actorCells_;
    NativeAmmoCells bareAmmoCells_;
    std::unordered_map<std::uint64_t,size_t> bareAmmoIndex_; // Derived after append/compaction/restore.
    std::uint32_t nextNativeGeneration_=1;
    NativeActorPool nativePool_;
    std::vector<NativePoolTileBody> nativeTileBodies_;
    std::optional<NativeTileBodyCatalog> nativeTileCatalog_;
    bool nativePoolOriginalHistoryKnown_=true;
    std::set<std::pair<std::size_t,std::uint32_t>> nativeRetainedHeldIdentities_;
    NativeActorPoolAllocation allocateNativePoolBody();
    void releaseNativePoolBody(std::size_t& slot);
    void resetNativePoolForLoad();
    void initializeNativeTileBodies();
    void releaseNativeHeldItem(HeldItem& held);
    void releaseNativeHeldItemsForReplacement(const std::vector<HeldItem>& incoming);
    void adoptNativeHeldItems();
    void rebuildNativeHeldOriginals();
    std::uint64_t nextNativeCellOrder_=1,nextNativeBodyIdentity_=1;
    void initializeNativeIdentity(size_t actor);
    void releaseNativeActorIdentity(size_t actor);
    void releaseNativeProjectile(Projectile&);
    void assignNativeCreator(size_t actor,std::optional<size_t> owner);
    std::optional<size_t> nativeAmmoOwner(const Projectile&) const;
    bool nativeAmmoPlayerOwned(Projectile&);
    void initializeBareAmmoBody(Projectile&);
    void synchronizeBareAmmoCell(const Projectile&);
    void rebuildBareAmmoIndex();
    void applyNativeBareAmmoDamage(std::uint64_t body,float amount,Projectile& source,bool playerOwned,
                                  bool sourceIsAmmo=true);
    void applyNativeContactDamage(std::size_t target,float amount);
    void applyNativeExplosionRadial(size_t source,float damage,float radius);
    void spawnProjectileBounceEffect(const Projectile&,ContentPosition source,ContentPosition nativePoint);
    void spawnWeaponSmoke(ClassId weapon,ContentPosition muzzle,float shooterAltitude);
    void spawnExplosions(const ObjectPlacement&,std::optional<size_t> creator,float altitude,bool topPhase=false,
                         std::optional<std::uint32_t> capturedCreatorGeneration={});
    void updateExplosion(size_t actor,float milliseconds);
    void updateExplosions(float milliseconds);
    const GafSequence* effectSequence(const SpriteReference&);
    ContentReader effectReader_;
    std::map<std::pair<std::string,uint32_t>,std::optional<GafSequence>> effectSequences_;
    void updateLifts(float milliseconds=0);
    void updateLift(std::size_t,float milliseconds);
    void configureGenerators(ActorState&,const ObjectPlacement&);
    void updateGenerators(float milliseconds);
    void updateGenerator(std::size_t,float milliseconds);
    bool generatorOccupied(size_t actor);
    void updateSaving(float milliseconds);
    void registerSavingPrice(size_t actor);
    uint64_t contentFingerprint() const;
    void destroy(size_t actor,std::optional<size_t> source);
    bool playerOwnsDamage(std::optional<size_t> creator) const;
    void updateCombatBonus(double milliseconds);
    void beginFinalEnding(size_t source);
    bool clearRoute(ContentPosition from,ContentPosition to,float radius) const;
    struct ActorBounds { double minX, minY, maxX, maxY; };
    std::vector<ActorBounds> actorBounds_, actorBoundGroups_;
    std::vector<size_t> actorBoundOrder_, actorBoundGroup_;
    size_t actorBoundsIndexedCount_=0;
    bool actorBoundsActive_=false;
    void updateActorBounds(size_t actor);
    std::optional<NativeNavGraph> spiderNavigation_;
    std::vector<size_t> spiderNavigationActors_;
    // Pure projection cache, excluded from WorldSnapshot and disk saves.
    std::vector<NativeProjectionCache> actorNavigationProjections_;
    NativeNavObstacle actorNavigationBounds(size_t actor,uint32_t subtype);
    void initializeSpiderNavigation(bool projectileNeeded=false);
    void restoreSpiderNavigationIndex();
    void clearSpiderNavigationRoutes(size_t actor);
    void updateSpiderNavigation(size_t actor,std::optional<size_t> closestNonContact={},bool synchronizeCell=true);
    void updateSpiderProjectileNavigation(Projectile& shot);
    void clearProjectileNavigation(Projectile& shot);
    std::optional<NativeNavObstacle> projectileNavigationObstacle(const Projectile&,std::optional<size_t> target,
        NativeNavPoint from,NativeNavPoint to,NativeNavPoint extent,bool overlap) const;
    bool lastSpiderSeparated_=false;
    float currentTickMilliseconds_=14; // transient caller interval for contact handoff
    std::optional<NativeNavObstacle> spiderNavigationObstacle(size_t actor,std::optional<size_t> target,
        NativeNavPoint from,NativeNavPoint to,NativeNavPoint extent,bool overlap,size_t projectileSelf=SIZE_MAX) const;
    bool advanceSpiderAI(size_t actor,float seconds,std::span<const ContentPosition> path);
    void configureOrdinaryEnemyAI(ActorState&,const ObjectPlacement&,bool legacy=false);
    bool advanceOrdinaryEnemyAI(size_t actor,float seconds,std::span<const ContentPosition> path);
    void clearOrdinaryEnemyRoutes(size_t actor);
    bool dynamicBlocked(size_t actor,ContentPosition from,ContentPosition to,float radius,bool ignorePlayer=false,bool nativeBounds=false) const;
    SceneContent* scene_=nullptr;
    const Level* level_=nullptr;
    size_t player_=0;
    size_t navigationCursor_=0;
    PlayerMotion motion_;
    MapView map_;
    double elapsedMs_=0;
    uint64_t playerKills_=0,ammoHits_=0,ammoMisses_=0;
    double fastKillMs_=0;
    uint64_t fastKillCount_=0;
    int64_t bonusEnemies_=0;
    std::optional<size_t> endingSource_;
    double endingRemainingMs_=0;
    // Collapsed attached FX timers are independently Managed in the original;
    // their stationary owner need not have an awake-manager entry.
    std::vector<size_t> attachedProtectionActors_;
    std::vector<ActorState> actors_;
    std::vector<Projectile> projectiles_;
    struct DamageRequest { size_t target;float value;std::optional<size_t> source;bool radial;bool ammoSource=false; };
    std::vector<DamageRequest> damageQueue_,contactDamage_;
    bool applyingDamage_=false;
    // Conservative absence proof. Once seen, retain the original scheduler until rebuild.
    bool legacyRouteSearchSeen_=false;
    std::optional<TransitionRequest> transition_;
    std::set<size_t> rejectedLifts_;
    std::vector<std::string> diagnostics_;
    void notifyWeapon();
    std::string message_;
    std::vector<HeldItem> heldItems_;
    std::mt19937 random_{0x56595255};
    NativeRandomState nativeWorldRandom_;
    NativeRandomState nativeGlobalRandom_;
    NativeRandomState nativeReplayRandom_;
    uint64_t catalogHash_=0;
    std::optional<size_t> savePlot_;
    double saveDwellMs_=0;
    bool saveAttempted_=false;
    std::optional<SaveRequest> saveRequest_;
};
}
