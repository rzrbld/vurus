#include "render/actor_sync_profile.h"
#include "render/native_render_cells.h"
#include "render/sprite_reachability.h"
#include "render/presentation.h"
#include "render/scene.h"
#include "render/fx_trail.h"
#include "game/level.h"
#include "game/properties.h"
#include "game/controls.h"
#include "game/triple_fire.h"
#include <cmath>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <set>



namespace vyruz {
namespace {
#if VURUS_SHADOW_VERIFY
struct ShadowEnvelopeAudit {
    unsigned long long layers=0,policy=0,sequence=0,tested=0,rejected=0;
    ~ShadowEnvelopeAudit(){std::fprintf(stderr,"SHADOW_ENVELOPE layers=%llu policy=%llu sequence=%llu tested=%llu rejected=%llu violations=0\n",layers,policy,sequence,tested,rejected);}
} shadowEnvelopeAudit;
struct ShadowVerifyScope { std::function<void()> finish; ~ShadowVerifyScope(){if(finish)finish();} };
#endif
constexpr auto timeWarpBank="common/data/object/fx/time-warp.gaf";
// Native431ae0 reads visible body230/gun23c for artwork. Idle controllers
// retain independent movement/weapon rings14c/18c, so logical heading is not
// a rendering substitute. Spider/player use their existing separate states.
int enemyVisibleBodyPose(const ActorState& actor) {
    return actor.enemyTurn?actor.enemyTurn->bodyPose-1:(actor.direction%24+24)%24;
}
int enemyVisibleGunPose(const ActorState& actor) {
    return actor.enemyTurn?actor.enemyTurn->gunPose-1:(actor.direction%24+24)%24;
}
std::string bankKey(std::string path) {
    path=folded(path);std::replace(path.begin(),path.end(),'\\','/');return path;
}

}
std::vector<SceneAudioCue> SceneRenderer::takeAudioCues() {
    auto result=std::move(audioCues_);audioCues_.clear();queuedAudioStarts_=0;return result;
}
void SceneRenderer::queueAudio(Channel& c,const GafSubframe& frame) {
    if(c.shadow || !(frame.command&1) || !frame.sample || queuedAudioStarts_>=256)return;
    const auto* selected=c.generatorBank?c.generatorBank:c.bank;
    for(const auto& [path,bank]:banks_)if(&bank==selected){
        if(!c.audioIdentity){if(!nextAudioIdentity_)++nextAudioIdentity_;c.audioIdentity=nextAudioIdentity_++;}
        audioCues_.push_back({c.object,path,frame.sample,SceneAudioCueKind::Play,c.audioIdentity,{},bool(frame.command&0x10000)});
        ++queuedAudioStarts_;break;
    }
}
void SceneRenderer::holdAudio(const Channel& c) {
    // 45e7e0 only refreshes owned sample volume/pan on an actual held-cursor
    // update. Skipped/sleeping/stopped ANMs must not acquire extra updates.
    const auto state=c.cursor.capture();
    if(c.audioIdentity && !(state.flags&8) && state.remaining>0)
        audioCues_.push_back({c.object,{},0,SceneAudioCueKind::Held,c.audioIdentity});
}
void SceneRenderer::forgetAudio(Channel& c) {
    // 45e770 clears ANM+30 without stopping its old sample. The consumer must
    // relinquish ownership, not stop whatever now occupies that audio channel.
    if(c.audioIdentity)audioCues_.push_back({c.object,{},0,SceneAudioCueKind::Forget,c.audioIdentity});
    c.audioIdentity=0;
}
std::optional<AnimationCursorState> SceneRenderer::itemCursor(size_t object,uint32_t sequence) const {
    for(const auto& c:channels_)if(c.object==object && !c.shadow && c.sequence==sequence)return c.cursor.capture();
    return {};
}
SceneRendererState SceneRenderer::capture() const {
    SceneRendererState result;result.objectCount=objectCount_;
    const auto channelState=[&](const Channel& c) {
        std::string path;
        for(const auto& [name,bank]:banks_)if(&bank==c.bank){path=name;break;}
        return SceneChannelState{c.object,c.shadow,path,c.sequence,c.cursor.capture(),c.gateRevision,c.firing,c.fireRevision,c.lift,c.visualRevision};
    };
    const auto effectChannel=[&](const Channel& c) {
        return SceneEffectChannelState{channelState(c),c.visibility,c.fadeClockMs,c.visibilityAlpha,
            c.walkerTurned,c.flightTilt,c.flightClockMs,c.flightPosition,c.flightPrevious};
    };
    auto& effects=result.effects.emplace();effects.orbitTimeMs=orbitTimeMs_;effects.resetEnemyFire=resetEnemyFire_;
    auto& terrain=effects.terrain.emplace();terrain.reserve(terrain_.size());
    effects.terrainClockMs=terrainClockMs_;
    for(const auto& c:terrain_) {
        std::string path;
        for(const auto& [name,bank]:banks_)if(&bank==c.bank){path=name;break;}
        terrain.push_back({c.placement.x,c.placement.y,c.placement.slot,c.placement.source,c.sequence,path,c.cursor.capture()});
        effects.terrainStartMs.push_back(c.startMs);
    }
    for(const auto& c:channels_) {
        if(nativeEnemyPhases_ && c.helicopter && !c.shadow && !c.derivedInvisible)effects.enemyLeans.push_back({c.object,c.flightTilt});
        if(c.bankedEnemy)effects.enemyLeans.push_back({c.object,c.enemyLean});
        if(c.derivedInvisible)effects.invisibleChannels.push_back(effectChannel(c));
        else result.channels.push_back(channelState(c));
    }
    for(const auto& c:enemyFire_)effects.enemyFire.push_back(effectChannel(c));
    for(const auto& orbit:enemyOrbits_)effects.orbits.push_back({orbit.object,orbit.config.sprite,
        orbit.state,orbit.visibility,orbit.fadeClockMs,orbit.visible,orbit.animationInitialized,
        orbit.cursor.capture(),orbit.parent,orbit.altitude});
    return result;
}
bool SceneRenderer::restore(const SceneRendererState& state,std::string& error) {
    actorSyncValid_=false;
    if(state.objectCount!=objectCount_ || state.channels.size()!=static_cast<size_t>(std::count_if(channels_.begin(),channels_.end(),[](const auto& c){return !c.derivedInvisible;}))) {
        error="saved render channels do not match scene";return false;
    }
    std::vector<Channel> restored;restored.reserve(channels_.size());
    std::set<std::tuple<size_t,bool,bool>> used;
    for(const auto& saved:state.channels) {
        const auto original=std::find_if(channels_.begin(),channels_.end(),[&](const auto& c){
            return !c.derivedInvisible && saved.object==c.object && saved.shadow==c.shadow && saved.firing==c.firing;
        });
        if(original==channels_.end() || !used.emplace(saved.object,saved.shadow,saved.firing).second) {
            error="invalid saved render channel identity";return false;
        }
        auto c=*original;c.enemyLean=0;
        auto cursor=saved.cursor;
        // Pre-overlay v11 saves timed the single static base frame instead.
        if(c.generatorBank && cursor.cursor==1) {
            const double activeFrameMs=c.generatorBank->animation.sequences[0].subframes[0].duration*14.;
            const double baseFrameMs=c.bank->animation.sequences[c.sequence-1].subframes[0].duration*14.;
            if(cursor.remaining>activeFrameMs && cursor.remaining<=baseFrameMs)cursor.remaining=activeFrameMs;
        }
        const auto bank=banks_.find(bankKey(saved.bankPath));
        if(saved.object!=c.object || saved.shadow!=c.shadow || saved.firing!=c.firing || saved.lift!=c.lift ||
           bank==banks_.end() || &bank->second!=c.bank || !saved.sequence || saved.sequence>c.bank->animation.sequences.size() ||
           !c.cursor.restore(c.generatorBank?c.generatorBank->animation.sequences[0]:c.bank->animation.sequences[saved.sequence-1],cursor)) {
            error="invalid saved render channel";return false;
        }
        c.pendingAudio.clear();
        if(c.helicopter && !c.shadow) {
            const auto bank=saved.sequence>=c.baseSequence?saved.sequence-c.baseSequence:0;
            c.flightTilt=bank>=3?25:bank==2?10:bank==1?5:0;
        }
        c.sequence=saved.sequence;c.gateRevision=saved.gateRevision;c.fireRevision=saved.fireRevision;c.visualRevision=saved.visualRevision;
        restored.push_back(std::move(c));
    }
    auto restoredOrbits=enemyOrbits_;
    auto restoredFire=enemyFire_;
    auto restoredTerrain=terrain_;
    double restoredTerrainClock=0;
    // Disk payloads omit transient scenery phases; reconstruct them even when
    // restoring into an already-running renderer, rather than retaining its age.
    for(auto& c:restoredTerrain)c.cursor.startTerrain(c.bank->animation.sequences[c.sequence-1]);
    double restoredTime=0;
    bool resetFire=true;
    if(state.effects) {
        const auto& effects=*state.effects;
        if(effects.terrain) {
            if(effects.terrain->size()!=restoredTerrain.size() || effects.terrainStartMs.size()!=restoredTerrain.size() ||
               !std::isfinite(effects.terrainClockMs) || effects.terrainClockMs<0){error="saved terrain channels do not match scene";return false;}
            restoredTerrainClock=effects.terrainClockMs;
            for(size_t i=0;i<restoredTerrain.size();++i) {
                auto& c=restoredTerrain[i];const auto& saved=(*effects.terrain)[i];
                const auto bank=banks_.find(bankKey(saved.bankPath));
                if(saved.x!=c.placement.x || saved.y!=c.placement.y || saved.slot!=c.placement.slot ||
                   saved.source!=c.placement.source || saved.sequence!=c.sequence || bank==banks_.end() ||
                   &bank->second!=c.bank || !c.cursor.restore(c.bank->animation.sequences[c.sequence-1],saved.cursor)) {
                    error="invalid saved terrain channel";return false;
                }
                if(effects.terrainStartMs[i]>=10000){error="invalid terrain start delay";return false;}
                c.startMs=effects.terrainStartMs[i];
            }
        }
        const auto finitePosition=[](ContentPosition p){return std::isfinite(p.x)&&std::isfinite(p.y);};
        const auto validFade=[](float visibility,float clock) {
            return std::isfinite(visibility)&&visibility>=0&&visibility<=49 &&
                std::isfinite(clock)&&clock>=0&&clock<14;
        };
        if(!std::isfinite(effects.orbitTimeMs)||effects.orbitTimeMs<0 ||
           effects.invisibleChannels.size()!=static_cast<size_t>(std::count_if(channels_.begin(),channels_.end(),[](const auto& c){return c.derivedInvisible;})) ||
           effects.enemyFire.size()!=enemyFire_.size() || effects.orbits.size()!=enemyOrbits_.size()) {
            error="saved effect channels do not match scene";return false;
        }
        const auto restoreEffectChannel=[&](const SceneEffectChannelState& saved,Channel& channel) {
            const auto& c=saved.channel;
            if(c.object!=channel.object || c.shadow!=channel.shadow || c.firing!=channel.firing || c.lift!=channel.lift ||
               !c.sequence || c.sequence>channel.bank->animation.sequences.size() ||
               !validFade(saved.visibility,saved.fadeClockMs) || !finitePosition(saved.flightPosition) || !finitePosition(saved.flightPrevious) ||
               !std::isfinite(saved.flightClockMs)||saved.flightClockMs<0 || saved.flightTilt<0 || saved.flightTilt>25)return false;
            const auto bank=banks_.find(bankKey(c.bankPath));
            if(bank==banks_.end() || &bank->second!=channel.bank ||
               !channel.cursor.restore(channel.bank->animation.sequences[c.sequence-1],c.cursor))return false;
            channel.sequence=c.sequence;channel.gateRevision=c.gateRevision;channel.fireRevision=c.fireRevision;
            channel.visualRevision=c.visualRevision;channel.visibility=saved.visibility;channel.fadeClockMs=saved.fadeClockMs;
            channel.visibilityAlpha=saved.visibilityAlpha;channel.walkerTurned=saved.walkerTurned;
            channel.flightTilt=saved.flightTilt;channel.flightClockMs=saved.flightClockMs;
            channel.flightPosition=saved.flightPosition;channel.flightPrevious=saved.flightPrevious;
            channel.pendingAudio.clear();return true;
        };
        std::set<std::tuple<size_t,bool,bool>> effectUsed;
        for(const auto& saved:effects.invisibleChannels) {
            const auto& c=saved.channel;
            const auto source=std::find_if(channels_.begin(),channels_.end(),[&](const auto& original) {
                return original.derivedInvisible && original.object==c.object && original.shadow==c.shadow && original.firing==c.firing;
            });
            if(source==channels_.end() || !effectUsed.emplace(c.object,c.shadow,c.firing).second) {
                error="invalid saved invisible channel identity";return false;
            }
            auto channel=*source;
            if(!restoreEffectChannel(saved,channel)){error="invalid saved invisible channel state";return false;}
            restored.push_back(std::move(channel));
        }
        std::set<size_t> leanObjects;
        for(const auto& lean:effects.enemyLeans) {
            auto channel=std::find_if(restored.begin(),restored.end(),[&](const auto& c){return c.object==lean.object&&(c.bankedEnemy||(c.helicopter&&!c.shadow&&!c.derivedInvisible));});
            if(channel==restored.end() || !leanObjects.insert(lean.object).second ||
               lean.value< (channel->helicopter?0:-(channel->leanSubtype==0x1a?1:15)) || lean.value>(channel->helicopter?25:channel->leanSubtype==0x1a?1:15)) {
                error="invalid saved enemy lean state";return false;
            }
            if(channel->helicopter)channel->flightTilt=lean.value;else channel->enemyLean=lean.value;
        }
        if(std::any_of(restored.begin(),restored.end(),[&](const auto& c){return c.bankedEnemy&&!leanObjects.contains(c.object);})) {
            error="saved enemy lean channels do not match scene";return false;
        }
        effectUsed.clear();
        for(const auto& saved:effects.enemyFire) {
            const auto& c=saved.channel;
            const auto source=std::find_if(restoredFire.begin(),restoredFire.end(),[&](const auto& original){return original.object==c.object;});
            if(source==restoredFire.end() || !effectUsed.emplace(c.object,c.shadow,c.firing).second || !restoreEffectChannel(saved,*source)) {
                error="invalid saved enemy firing state";return false;
            }
        }
        for(size_t i=0;i<effects.orbits.size();++i) {
            const auto& saved=effects.orbits[i];auto& orbit=restoredOrbits[i];const auto& history=saved.history;
            if(saved.object!=orbit.object || bankKey(saved.sprite.path)!=bankKey(orbit.config.sprite.path) ||
               saved.sprite.sequence.value!=orbit.config.sprite.sequence.value || !validFade(saved.visibility,saved.fadeClockMs) ||
               !finitePosition(saved.parent)||!std::isfinite(saved.altitude) ||
               !std::isfinite(history.ageMs)||history.ageMs<0 || !std::isfinite(history.nextTrailMs)||history.nextTrailMs<0 ||
               (history.initialized?history.particles.size()!=size_t(orbit.config.count):!history.particles.empty()) ||
               (!history.initialized && !history.trails.empty())) {
                error="invalid saved attached effect state";return false;
            }
            for(const auto& particle:history.particles)if(!finitePosition(particle.position)||!std::isfinite(particle.altitude)||
                !std::isfinite(particle.ageMs)||particle.ageMs<0){error="invalid saved orbit particle";return false;}
            for(const auto& trail:history.trails)if(!finitePosition(trail.position)||!std::isfinite(trail.altitude)||
                !std::isfinite(trail.ageMs)||trail.ageMs<0 || trail.ageMs>=orbit.config.trailLifetimeMs || orbit.config.trailSprite.path.empty()) {
                error="invalid saved attached trail";return false;
            }
            const auto bank=banks_.find(bankKey(orbit.config.sprite.path));
            if(bank==banks_.end() || !saved.sprite.sequence.value || saved.sprite.sequence.value>bank->second.animation.sequences.size() ||
               !orbit.cursor.restore(bank->second.animation.sequences[saved.sprite.sequence.value-1],saved.cursor)) {
                error="invalid saved attached animation";return false;
            }
            orbit.state=history;orbit.visibility=saved.visibility;orbit.fadeClockMs=saved.fadeClockMs;orbit.visible=saved.visible;
            orbit.animationInitialized=saved.animationInitialized;orbit.parent=saved.parent;orbit.altitude=saved.altitude;
        }
        restoredTime=effects.orbitTimeMs;resetFire=effects.resetEnemyFire;
    } else {
        // Native disk load reconstructs these No-saving FX and fade counters.
        // Legacy port saves likewise contain no derived presentation payload.
        for(auto c:channels_)if(c.derivedInvisible){c.visibility=0;c.fadeClockMs=0;c.visibilityAlpha=true;c.enemyLean=0;restored.push_back(std::move(c));}
        for(auto& orbit:restoredOrbits){orbit.state={};orbit.visibility=0;orbit.fadeClockMs=0;orbit.visible=false;orbit.animationInitialized=false;}
    }
    for(auto& c:restored)c.audioIdentity=0;
    for(auto& c:restoredFire)c.audioIdentity=0;
    for(auto& c:restoredTerrain)c.audioIdentity=0;
    channels_=std::move(restored);enemyOrbits_=std::move(restoredOrbits);enemyFire_=std::move(restoredFire);rebuildEnemyObjectChannels();
    terrain_=std::move(restoredTerrain);
    terrainClockMs_=restoredTerrainClock;
    playerTurn_={};
    orbitTimeMs_=restoredTime;audioCues_.clear();queuedAudioStarts_=0;resetEnemyFire_=resetFire;error.clear();return true;
}
bool SceneRenderer::decodeBank(const std::string& path,GafAnimation& animation) {
    std::vector<uint8_t> data;
    if(!read_ || !read_(path,data))return false;
    const auto use=sequenceUses_.find(bankKey(path));
    if(use==sequenceUses_.end() || use->second.all)return decodeGafAnimation(data,animation);
    return decodeGafAnimation(data,animation,GafDecodeMode::SelectedSequences,
        [&](const GafAnimation& metadata){return use->second.select(metadata);});
}
bool SceneRenderer::load(const SceneContent& scene,const ContentReader& read,std::string& error) {
    if (!banks_.empty()) { error="scene renderer must be cleared before reload"; return false; }
    read_=read;
    sequenceUses_=sceneSpriteSequenceUses(scene);
    if(!sync(scene,error))return false;
    if(scene.player) {
        auto [it,inserted]=banks_.try_emplace(timeWarpBank);
        if(inserted) {
            if(!decodeBank(timeWarpBank,it->second.animation) ||
               it->second.animation.sequences.empty() || it->second.animation.sequences[0].subframes.empty()) {
                error="cannot decode Time Warp effect";return false;
            }
            it->second.textures.resize(it->second.animation.frames.size());
        }
    }
    for(int group=0;group<9;++group) {
        auto& sprite=orbitSprites_[group];sprite={};orbitAltitudes_[group]={};
        const auto name=group==0?std::string("No-Inertia-Control-FX-1"):
            group<6?"Invulnerability-FX-"+std::to_string(group):
            "Touch-And-Destroy-FX-"+std::to_string(group-5);
        const auto* cls=findClass(scene,name);if(!cls)continue;
        sprite=cls->sprite;orbitAltitudes_[group]=attachedFxAltitudeConfig(*cls);
        auto [it,inserted]=banks_.try_emplace(bankKey(sprite.path));
        if(inserted) {
            if(!decodeBank(sprite.path,it->second.animation)) {
                error="cannot decode orbit effect: "+sprite.path;return false;
            }
            it->second.textures.resize(it->second.animation.frames.size());
        }
    }
    return true;
}
void SceneRenderer::preloadClassSprites(const SceneContent& scene) {
    std::set<std::string> attempted;
    const auto reachable=reachableSpriteClasses(scene);
    for(size_t i=0;i<scene.classes.size();++i)if(reachable[i])for(const auto* ref:{&scene.classes[i].sprite,&scene.classes[i].shadow}) {
        if(!ref->sequence.value || ref->path.empty())continue;
        const auto key=bankKey(ref->path);
        if(banks_.contains(key)||!attempted.insert(key).second)continue;
        Bank bank;
        // Unused catalog entries need not exist. Preserve lazy-load failure
        // behavior for them; never leave a failed/partial bank in the cache.
        if(!decodeBank(ref->path,bank.animation))continue;
        bank.textures.resize(bank.animation.frames.size());
        banks_.emplace(key,std::move(bank));
    }
}
bool SceneRenderer::loadTerrain(const NativeTileBodyCatalog& catalog,std::string& error,NativeRandomState presentationRandom) {
    if(!terrain_.empty() || !terrainCells_.empty()){error="terrain renderer must be cleared before reload";return false;}
    if(catalog.cells.size()!=size_t(catalog.width)*catalog.height){error="invalid terrain render grid";return false;}
    // Terrain may share a bank with actors. Union every source before decoding;
    // an already loaded partial bank is expanded only here, during level load.
    std::set<std::string> expanded;
    for(const auto& source:catalog.sources)if(!source.resolvedResource.empty() && source.sequence) {
        const auto key=bankKey(source.resolvedResource);
        auto& use=sequenceUses_[key];
        if(use.sequences.insert(source.sequence).second && !use.all && banks_.contains(key))expanded.insert(key);
    }
    for(const auto& key:expanded) {
        auto& bank=banks_.at(key);bank.envelopes.clear();
        if(!decodeBank(key,bank.animation)){error="cannot expand shared terrain bank: "+key;return false;}
    }
    std::vector<TerrainChannel> channels;
    std::vector<std::array<size_t,4>> cells(catalog.cells.size());
    for(auto& cell:cells)cell.fill(SIZE_MAX);
    //41f26f consumes one inclusive source-frame draw per Anim source before
    //433930 creates bodies. Source ANMs are not copied into those fresh bodies.
    // Keep cosmetic initialization on a copied context, preserving the existing
    // gameplay stream; whole-host global RNG phase remains a documented boundary.
    for(const auto& source:catalog.sources)if(source.kind==1 && (source.flags&1)) {
        auto [it,inserted]=banks_.try_emplace(bankKey(source.resolvedResource));
        if(inserted) {
            if(!decodeBank(source.resolvedResource,it->second.animation)) {
                error="cannot decode terrain animation: "+source.resolvedResource;return false;
            }
            it->second.textures.resize(it->second.animation.frames.size());
        }
        if(!source.sequence || source.sequence>it->second.animation.sequences.size()) {
            error="invalid terrain animation sequence: "+source.resolvedResource;return false;
        }
        presentationRandom.inclusive(1,int(it->second.animation.sequences[source.sequence-1].subframes.size()));
    }
    const bool ok=visitNativeTileBodyPlacements(catalog,[&](auto placement) {
        const auto& source=catalog.sources[placement.source];
        if(source.resolvedResource.empty() || !source.sequence){error="unbound terrain animation";return false;}
        auto [it,inserted]=banks_.try_emplace(bankKey(source.resolvedResource));
        auto& bank=it->second;
        if(inserted) {
            if(!decodeBank(source.resolvedResource,bank.animation)) {
                error="cannot decode terrain animation: "+source.resolvedResource;return false;
            }
            bank.textures.resize(bank.animation.frames.size());
        }
        if(source.sequence>bank.animation.sequences.size() || bank.animation.sequences[source.sequence-1].subframes.empty()) {
            error="invalid terrain animation sequence: "+source.resolvedResource;return false;
        }
        const uint32_t startMs=(uint64_t(presentationRandom.raw())*10000)>>32; //454130(10000), exclusive upper bound.
        TerrainChannel c{placement,&bank,source.sequence,source.subtype,startMs,{}};
        c.cursor.startTerrain(bank.animation.sequences[c.sequence-1]);
        cells[size_t(placement.y)*catalog.width+placement.x][placement.slot]=channels.size();
        channels.push_back(std::move(c));return true;
    });
    if(!ok)return false;
    terrain_=std::move(channels);terrainCells_=std::move(cells);
    terrainWidth_=catalog.width;terrainHeight_=catalog.height;terrainClockMs_=0;error.clear();return true;
}
void SceneRenderer::advanceTerrain(double milliseconds,const EnemyAnimationView& view) {
    if(!groundDetails || view.playerOnly)return;
    const auto advance=[&](TerrainChannel& c) {
        if(c.startMs>=terrainClockMs_)return;
        const auto source=std::array<float,3>{float(c.placement.x*64),float(c.placement.y*32),16.f};
        const auto current=c.cursor.capture();
        if(c.audioIdentity && !(current.flags&8) && current.remaining>milliseconds)
            audioCues_.push_back({SIZE_MAX,{},0,SceneAudioCueKind::Held,c.audioIdentity,source});
        c.cursor.advance(c.bank->animation.sequences[c.sequence-1],milliseconds,false,[&](const auto& frame) {
            if(!(frame.command&1) || !frame.sample || queuedAudioStarts_>=256)return;
            if(!c.audioIdentity){if(!nextAudioIdentity_)++nextAudioIdentity_;c.audioIdentity=nextAudioIdentity_++;}
            for(const auto& [path,bank]:banks_)if(&bank==c.bank) {
                audioCues_.push_back({SIZE_MAX,path,frame.sample,SceneAudioCueKind::Play,c.audioIdentity,source,bool(frame.command&0x10000)});
                ++queuedAudioStarts_;break;
            }
        });
    };
    if(view.unboundedOracle) {
        for(auto& c:terrain_)advance(c);
        return;
    }
    //433b30 cell admission; factory prepends slots0,1,2,3, so visit3..0.
    const auto start=[](int32_t translation,unsigned shift,int margin) {
        return translation>0?0:std::max(0,int((uint32_t(0)-uint32_t(translation))>>shift)-margin);
    };
    const int left=start(view.translationX,6,1),top=start(view.translationY,5,2);
    const int right=std::min(int(terrainWidth_),int(view.width>>6)+3+left);
    const int bottom=std::min(int(terrainHeight_),int(view.height>>5)+5+top);
    for(int y=top;y<bottom;++y)for(int x=left;x<right;++x)
        for(int slot=3;slot>=0;--slot) {
            const auto id=terrainCells_[size_t(y)*terrainWidth_+x][slot];
            if(id==SIZE_MAX)continue;
            advance(terrain_[id]);
        }
}
bool SceneRenderer::sync(const SceneContent& scene,std::string& error) {
    const size_t previousChannels=channels_.size();
    objectChannels_.resize(scene.objects.size());
    objectFireChannels_.resize(scene.objects.size(),SIZE_MAX);
    const std::optional<size_t> player=scene.player?std::optional<size_t>(scene.player->value):std::nullopt;
    if(playerObject_!=player){playerObject_=player;playerTurn_={};}
    std::set<unsigned> unsupported;
    for (size_t i=objectCount_;i<scene.objects.size();++i) {
        const auto& object=scene.objects[i];
        // Player invisibility is transient Drone state, with its own
        // controlled-body alpha floor. Keep its persisted channels even on reload.
        const bool invisible=(!scene.player || i!=scene.player->value) && object.flags.invisible.value_or(false);
        if(invisible && !propertyBool(object.properties,"Visible-when-firing"))continue;
        for(auto config:enemyOrbitConfigs(scene,object)) {
            if(!invisible && !config.timedProtection)continue;
            for(const auto& sprite:{config.sprite,config.trailSprite}) {
                if(sprite.path.empty())continue;
                auto [it,inserted]=banks_.try_emplace(bankKey(sprite.path));
                if(inserted){
                    if(!decodeBank(sprite.path,it->second.animation)){error="cannot decode enemy orbit: "+sprite.path;return false;}
                    it->second.textures.resize(it->second.animation.frames.size());}
            }
            enemyOrbits_.push_back({i,std::move(config),{}});
        }
        for (bool shadow:{true,false}) {
            const auto& reference=shadow?object.shadow:object.sprite;
            if (!reference.sequence.value || reference.path.empty()) continue;
            auto [it,inserted]=banks_.try_emplace(bankKey(reference.path));
            auto& bank=it->second;
            if (inserted) {
                if (!decodeBank(reference.path,bank.animation)) {
                    error="cannot decode placed sprite: "+reference.path; return false;
                }
                bank.textures.resize(bank.animation.frames.size());
            }
            uint32_t sequence=reference.sequence.value;
            const auto* lift=shadow?nullptr:attribute(object.properties,"lift");
            if(lift && !propertyBool(lift->children,"Present")) ++sequence;
            if (!shadow && scene.player && object.id==*scene.player) sequence=25;
            if (sequence>bank.animation.sequences.size()) { error="invalid sequence: "+reference.path; return false; }
            channels_.push_back({i,shadow,&bank,sequence,{}});
            auto& channel=channels_.back();
            channel.derivedInvisible=invisible;
            const auto& animation=bank.animation.sequences[sequence-1];
            channel.lift=lift!=nullptr;
            channel.baseSequence=reference.sequence.value;
            channel.turretBase=!shadow && objectSubtype(object)=="turret" &&
                bank.animation.sequences.size()>=25;
            const auto family=objectSubtype(object);
            channel.nativeEnemy=family=="cop"||family=="burner"||family=="walker"||family=="jeep"||family=="roller"||family=="heli"||family=="turret"||family=="tank"||family=="spider"||family=="staticgen";
            const auto* cls=object.classId.value<scene.classes.size()?&scene.classes[object.classId.value]:nullptr;
            channel.nativeGraphicUpdate=propertyBool(cls?cls->properties:object.properties,"Graphic-update-needed");
            // Item (12) without Graphic-update-needed uses the native cell producer.
            // Saving-plot body playback requires the selected world20c8 actor.
            channel.nativeItem=family=="item"&&!channel.nativeGraphicUpdate&&!invisible;
            channel.nativeSavingPlot=propertyBool(cls?cls->properties:object.properties,"Saving-plot");
            channel.nativeInGround=(cls?cls->flags:object.flags).inGround.value_or(false);
            channel.nativeDetail=(cls?cls->flags:object.flags).detail.value_or(false);
            const size_t leanBanks=family=="cop"?31:family=="burner"?27:0;
            channel.bankedEnemy=!shadow && leanBanks && bank.animation.sequences.size()>=leanBanks &&
                bank.animation.sequences[24].subframes.size()>=48;
            channel.leanSubtype=family=="cop"?0x16:family=="burner"?0x1a:0;
            channel.tank=!shadow && family=="tank" && bank.animation.sequences.size()>=25 &&
                bank.animation.sequences[24].subframes.size()>=24;
            // 431ae0 selects an angle inside the authored shadow sequence.
            // -1 keeps timed animation (notably Spider's separate group path),
            // 0 holds the base pose, 1/2 indexes the 24/48-entry direction bank.
            if(shadow) {
                const auto family=objectSubtype(object);
                if(family=="cop" || family=="burner")channel.shadowPoseStride=2;
                else if(family=="jeep" || family=="roller" || family=="heli" || family=="tank")channel.shadowPoseStride=1;
                else if(family=="walker" || family=="turret")channel.shadowPoseStride=0;
            }
            channel.helicopter=objectSubtype(object)=="heli" && animation.subframes.size()>=24 &&
                (shadow || sequence+3<=bank.animation.sequences.size());
            channel.flightPosition=channel.flightPrevious=object.position;
            channel.walker=!shadow && objectSubtype(object)=="walker" && sequence+23<=bank.animation.sequences.size();
            // 417b30 case17: static base in the primary bank; active animation
            // is sequence1 of the secondary bank, independently of shadow2.
            // Reuse the body's saved cursor for that animation: the shipped
            // generator base contains one fixed frame and has no running pose.
            if(!shadow && objectSubtype(object)=="staticgen") {
                const auto secondary=banks_.find(bankKey(object.shadow.path));
                if(secondary!=banks_.end() && !secondary->second.animation.sequences.empty())
                    channel.generatorBank=&secondary->second;
            }
            channel.spider=objectSubtype(object)=="spider" &&
                bank.animation.sequences.size()>=channel.baseSequence+3 &&
                bank.animation.sequences.size()>=5 &&
                (shadow || bank.animation.sequences[4].subframes.size()>=24);
            // 417b30 case13 selects raised/base for Present, lowered/base+1
            // otherwise, then places the cursor on the final authored frame.
            if(channel.lift)channel.cursor.holdLast(animation);
            else channel.cursor.reset(channel.generatorBank?channel.generatorBank->animation.sequences[0]:animation,[&](const auto& frame){if(channel.pendingAudio.size()<256)channel.pendingAudio.push_back(frame);});
            for (const auto& frame:animation.subframes) {
                const auto opcode=frame.flags&0xff;
                if (opcode!=0 && opcode!=1 && opcode!=6 && opcode!=7 && opcode!=8 && opcode!=9) unsupported.insert(opcode);
            }
        }
    }
    // Player muzzle streaks are a separate authored channel in the secondary
    // bank (417b30), not the invisible Gatling ammo's placeholder GAF.
    if (scene.player && scene.player->value>=objectCount_ && scene.player->value<scene.objects.size()) {
        const auto id=scene.player->value;const auto& object=scene.objects[id];
        const auto bank=banks_.find(bankKey(object.shadow.path));
        if(bank!=banks_.end() && bank->second.animation.sequences.size()>=2) {
            Channel fire{id,false,&bank->second,2,{}};fire.firing=true;
            channels_.push_back(std::move(fire));
        }
    }
    // Native 413820 classifies actors by explicit Sub-type or the GAF parent.
    // 417b30 binds Cop (0x16) muzzle sequence2 and Turret (0x1e) sequence26.
    // Keep these derived channels outside persisted identities for old saves.
    for(size_t id=objectCount_;id<scene.objects.size();++id) {
        const auto& object=scene.objects[id];
        if(scene.player && id==scene.player->value)continue;
        const auto kind=objectSubtype(object);
        const uint32_t sequence=kind=="cop"?2:kind=="turret"?26:0;
        const auto bank=banks_.find(bankKey(object.shadow.path));
        if(!sequence||bank==banks_.end()||bank->second.animation.sequences.size()<sequence)continue;
        Channel fire{id,false,&bank->second,sequence,{}};fire.firing=true;
        fire.derivedInvisible=object.flags.invisible.value_or(false);
        objectFireChannels_[id]=enemyFire_.size();
        enemyFire_.push_back(std::move(fire));
    }
    for(size_t i=previousChannels;i<channels_.size();++i)
        {objectChannels_[channels_[i].object].push_back(i);markDrawBinding(i);}
    if(channels_.size()!=previousChannels)refreshChannelScan(previousChannels);
    for(size_t i=0;i<channelScan_.size();++i)if(channelScan_[i].helicopter)
        channels_[i].flightPosition=scene.objects[channelScan_[i].object].position;
    for(auto& orbit:enemyOrbits_) {
        const auto& object=scene.objects[orbit.object];orbit.parent=object.position;
        orbit.altitude=spriteProperties(object).orbitAltitude;
    }
    const bool newObjects=objectCount_!=scene.objects.size();
    const bool shrank=scene.objects.size()<objectCount_;
    objectCount_=scene.objects.size();
    if(shrank)rebuildEnemyObjectChannels();
    else if(newObjects) {
        // sync only appends channels. Keep existing indices and inspect only
        // newly bound channels; ordinary FX births must not rescan all actors.
        // restoreState reorders channels and still uses the full rebuild.
        enemyObjectChannels_.resize(objectCount_);
        for(size_t index=previousChannels;index<channels_.size();++index)
            if(channels_[index].nativeEnemy&&channels_[index].object<objectCount_)
                enemyObjectChannels_[channels_[index].object].push_back(index);
    }
    for (auto opcode:unsupported) std::fprintf(stderr,"render diagnostic: opcode %u uses color-key fallback; native blend unresolved\n",opcode);
    return true;
}
void SceneRenderer::releaseTextures(Gfx& gfx) {
    textureResidency_.clear();
    for(auto& [path,bank]:banks_)for(auto& texture:bank.textures){gfx.deleteTexture(texture);texture=0;}
}
void SceneRenderer::clear(Gfx& gfx) {
    actorSyncValid_=false;synchronizedActors_=nullptr;
    objectChannels_.clear();objectFireChannels_.clear();pendingDrawBindings_.clear();nativeDrawView_.reset();
    spriteProperties_.clear();
    sequenceUses_.clear();
    releaseTextures(gfx);
    channelScan_.clear();workingChannels_.clear();workingOrderDirty_=true;
    playerTurn_={};playerObject_.reset();
    terrain_.clear();terrainCells_.clear();terrainWidth_=terrainHeight_=0;
    terrainClockMs_=0;
    audioCues_.clear();queuedAudioStarts_=0;enemyObjectChannels_.clear();channels_.clear();enemyFire_.clear();resetEnemyFire_=false; banks_.clear(); objectCount_=0;read_={};enemyOrbits_.clear();orbitTimeMs_=0;
}
void SceneRenderer::rebuildEnemyObjectChannels() {
    objectChannels_.assign(objectCount_,{});objectFireChannels_.assign(objectCount_,SIZE_MAX);
    pendingDrawBindings_.clear();
    for(size_t i=0;i<channels_.size();++i){channels_[i].drawBindingPending=false;markDrawBinding(i);}
    for(size_t i=0;i<channels_.size();++i)if(channels_[i].object<objectCount_)objectChannels_[channels_[i].object].push_back(i);
    for(size_t i=0;i<enemyFire_.size();++i)if(enemyFire_[i].object<objectCount_)objectFireChannels_[enemyFire_[i].object]=i;
    nativeDrawView_.reset();
    refreshChannelScan();
    enemyObjectChannels_.resize(objectCount_);
    for(auto& indices:enemyObjectChannels_)indices.clear();
    for(size_t index=0;index<channels_.size();++index)if(channels_[index].nativeEnemy&&channels_[index].object<objectCount_)
        enemyObjectChannels_[channels_[index].object].push_back(index);
}
void SceneRenderer::refreshChannelScan(size_t first) {
    actorSyncValid_=false;
    workingOrderDirty_=true;
    if(first>channelScan_.size())first=channelScan_.size();
    channelScan_.resize(first);
    for(size_t i=first;i<channels_.size();++i) {
        const auto& c=channels_[i];
        channelScan_.push_back({c.object,c.visualRevision,
            !c.walker&&!c.spider&&!c.generatorBank&&!c.derivedInvisible&&
            !c.nativeEnemy&&!c.helicopter&&!c.lift&&!c.firing,c.helicopter});
    }
    workingChannels_.resize(channels_.size());
    for(size_t i=0;i<channels_.size();++i)workingChannels_[i]=i;
}
bool SceneRenderer::actorsAlreadySynchronized(const std::vector<ActorState>& actors,std::optional<uint64_t> revision) const {
    // Native enemy channels own their phase-specific selectors. Legacy fixtures
    // may reset stateful Spider/generator channels even with unchanged actors.
    return reuseActorSync_&&nativeEnemyPhases_&&actorSyncValid_&&revision&&
        synchronizedActors_==&actors&&synchronizedActorCount_==actors.size()&&
        synchronizedActorRevision_==*revision;
}
void SceneRenderer::rememberActorSync(const std::vector<ActorState>& actors,std::optional<uint64_t> revision) {
    actorSyncValid_=revision.has_value()&&nativeEnemyPhases_;
    synchronizedActors_=&actors;synchronizedActorCount_=actors.size();
    synchronizedActorRevision_=revision.value_or(0);
}
void SceneRenderer::syncActors(const std::vector<ActorState>& actors) {
#if VURUS_ACTOR_SYNC_PROFILE
    actorSyncProfile::Scope profile(channelScan_.size());
#endif
    actorSyncValid_=false;
    if(resetEnemyFire_) {
        for(auto& c:enemyFire_)if(c.object<actors.size())c.fireRevision=actors[c.object].fireRevision;
        resetEnemyFire_=false;
    }
    for(size_t i=0;i<channelScan_.size();++i) {
        auto& scan=channelScan_[i];
        const bool wasInactive=scan.inactive;
        const auto* actor=scan.object<actors.size()?&actors[scan.object]:nullptr;
        if(actor&&scan.visualRevision!=actor->visualRevision)markDrawBinding(i);
        if(actor && scan.passive&&!actor->gate&&!actor->lift&&scan.visualRevision==actor->visualRevision) {
            if(!actor->alive && !scan.inactive)channels_[i].pendingAudio.clear();
            scan.inactive=!actor->alive;
        } else {
            // Keep the ordering key current even for firing/native channels
            // whose pose synchronization is admitted in a separate phase.
            if(actor)scan.visualRevision=actor->visualRevision;
            scan.inactive=false;
            auto& c=channels_[i];
            if(!c.firing && c.object<actors.size() && !(nativeEnemyPhases_&&c.nativeEnemy)) {
                syncActorChannel(c,actors[c.object]);
                scan.visualRevision=c.visualRevision;
            }
        }
        const auto birth=scan.visualRevision>>32;
        const uint64_t key=actor?(birth?birth-1:uint64_t(scan.object)):uint64_t(scan.object);
        if(wasInactive!=scan.inactive || scan.orderKey!=key)workingOrderDirty_=true;
        scan.orderKey=key;
    }
    if(workingOrderDirty_) {
        // Rebuild ascending channel indices before the stable sort so equal
        // births retain the original channel-index tie order after reuse.
        workingChannels_.clear();
        for(size_t i=0;i<channelScan_.size();++i)if(!channelScan_[i].inactive)workingChannels_.push_back(i);
        std::stable_sort(workingChannels_.begin(),workingChannels_.end(),[&](size_t a,size_t b){return channelScan_[a].orderKey<channelScan_[b].orderKey;});
        workingOrderDirty_=false;
    }

}
void SceneRenderer::syncActorChannel(Channel& c,const ActorState& actor) {
        if(c.visualRevision!=actor.visualRevision) {
            // App advances existing channels before binding newly appended
            // objects. A reused record must receive that same newborn phase.
            if(actor.reusedRecord)c.skipNewbornAdvance=true;
            c.visualRevision=actor.visualRevision;c.pendingAudio.clear();c.enemyLean=0;
            forgetAudio(c);c.cursor.reset(c.bank->animation.sequences[c.sequence-1],[&](const auto& frame){if(c.pendingAudio.size()<256)c.pendingAudio.push_back(frame);});
        }
        if(c.walker) {
            const uint32_t chosen=c.baseSequence+enemyVisibleBodyPose(actor);
            if(c.sequence!=chosen) {
                const auto phase=c.cursor.capture();c.sequence=chosen;c.pendingAudio.clear();
                if(!c.cursor.restore(c.bank->animation.sequences[chosen-1],phase)) {
                    forgetAudio(c);c.cursor.reset(c.bank->animation.sequences[chosen-1]);
                }
                c.walkerTurned=true;
            }
        }
        if(c.spider) {
            // Native 417b30 / 431ae0: four walk banks, sequence 5 for
            // standing/turning body poses; stopped shadows hold their first pose.
            // 431ae0 uses forward command 200 at a cardinal pose, not
            // displacement: native propulsion can alternate moving/zero ticks.
            const uint32_t sector=((actor.direction%24+24)%24)/6;
            const bool walk=actor.walkingCommand&&(!nativeEnemyPhases_||(actor.direction%6)==0);
            const uint32_t chosen=walk||c.shadow?c.baseSequence+sector:5;
            if(c.sequence!=chosen || !walk) {
                c.sequence=chosen;c.pendingAudio.clear();
                forgetAudio(c);c.cursor.reset(c.bank->animation.sequences[chosen-1]);
            }
        }
        if(c.generatorBank && !actor.solid) {
            c.pendingAudio.clear();forgetAudio(c);c.cursor.reset(c.generatorBank->animation.sequences[0]);
        }
        if(c.shadow)return;
        if((!actor.gate&&!actor.lift)||c.gateRevision==actor.gateRevision)return;
        const auto sequence=actor.lift?actor.liftState.sequence.value:actor.gateState.sequence.value;
        if(!sequence||sequence>c.bank->animation.sequences.size())return;
        c.sequence=sequence;c.gateRevision=actor.gateRevision;
        const auto& s=c.bank->animation.sequences[sequence-1];
        c.pendingAudio.clear();forgetAudio(c);
        if(c.gateRevision==1)c.cursor.holdLast(s);else c.cursor.startGateTransition(s,[&](const auto& frame){if(c.pendingAudio.size()<256)c.pendingAudio.push_back(frame);});
}
void SceneRenderer::advanceEnemyChannels(size_t id,double milliseconds,const std::vector<ActorState>& actors,EnemyAnimationPhase phase,bool visibilityStep) {
    if(id>=actors.size()||id>=enemyObjectChannels_.size()||!actors[id].alive)return;
    const auto& actor=actors[id];
    for(const auto index:enemyObjectChannels_[id]) {
        auto& c=channels_[index];
        syncActorChannel(c,actor);
        //431ae0 selector helpers run before the update-phase return and are
        //independent of bVar3: one invocation, not elapsed-time catch-up.
        if(c.bankedEnemy) {
            auto turn=actor.enemyTurn.value_or(initializeEnemyTurn({c.leanSubtype},actor.direction,0));
            turn.lean=c.enemyLean;advanceEnemyLean({c.leanSubtype},turn);c.enemyLean=turn.lean;
        }
        if(c.helicopter&&!c.shadow) {
            const float magnitude=actor.enemyMagnitude;
            if(magnitude>2){if(c.flightTilt<25)++c.flightTilt;}
            else {const int threshold=magnitude>1?20:magnitude>.5f?10:5;c.flightTilt+=c.flightTilt>threshold?-1:1;}
            c.flightClockMs=0;
        }
        // Select the native body bank during BOTH phases, rather than waiting
        // for draw() to mutate the channel after an update used the old bank.
        if(!c.shadow) {
            uint32_t chosen=c.sequence;
            if(c.bankedEnemy) {
                const int magnitude=std::abs(c.enemyLean);
                const int stage=!magnitude?-1:c.leanSubtype==0x16?(magnitude<6?0:magnitude<=10?1:2):0;
                chosen=stage<0?25:c.leanSubtype==0x16?(c.enemyLean<0?26:29)+stage:c.enemyLean<0?26:27;
            }else if(c.helicopter)chosen=c.baseSequence+(c.flightTilt<5?0:c.flightTilt<10?1:c.flightTilt<20?2:3);
            else if(c.tank)chosen=c.baseSequence+enemyVisibleBodyPose(actor);
            else if(!c.walker&&!c.spider&&!c.generatorBank&&c.bank->animation.sequences.size()>=c.baseSequence+23)
                chosen=c.baseSequence+(c.turretBase?enemyVisibleGunPose(actor):enemyVisibleBodyPose(actor));
            if(chosen!=c.sequence) {
                c.sequence=chosen;c.pendingAudio.clear();forgetAudio(c);c.cursor.reset(c.bank->animation.sequences[chosen-1]);
            }
            if(c.bankedEnemy||c.helicopter) {
                const auto& sequence=c.bank->animation.sequences[c.sequence-1];
                const auto pose=uint32_t(enemyVisibleBodyPose(actor)+1);
                if(pose<=sequence.subframes.size())c.cursor.restore(sequence,
                    {pose,sequence.flags,double(sequence.subframes[pose-1].duration)*14});
            }
        }
        // Reveal is after phase!=0 return; native world20&100 permits one
        // increment per admitted draw. The enabled port presentation is the
        // existing worldflag adapter; its old14ms path is left for fixtures.
        if(phase==EnemyAnimationPhase::Draw&&visibilityStep&&c.derivedInvisible) {
            if(actor.enemyRevealed) {
                c.visibilityAlpha=c.visibility<49;
                c.visibility=std::min(49.f,c.visibility+1);
            }else {c.visibilityAlpha=true;c.visibility=std::max(0.f,c.visibility-1);}
            c.fadeClockMs=0;
        }
        const bool update=phase!=EnemyAnimationPhase::Draw||!c.nativeGraphicUpdate;
        if(!update){c.pendingAudio.clear();continue;}
        if((c.turretBase&&actor.turretOnlyBase)||c.shadowPoseStride>=0||c.bankedEnemy||c.helicopter){c.pendingAudio.clear();continue;}
        const bool spiderWalk=actor.walkingCommand&&(actor.direction%6)==0;
        if(c.spider&&!spiderWalk){c.pendingAudio.clear();continue;}
        if(c.walker) {
            const bool run=actor.walkingCommand||c.walkerTurned;c.walkerTurned=false;
            if(!run){c.pendingAudio.clear();continue;}
        }
        for(const auto& frame:c.pendingAudio)queueAudio(c,frame);
        c.pendingAudio.clear();
        if(c.generatorBank&&!actor.solid)continue;
        const auto& sequence=c.generatorBank?c.generatorBank->animation.sequences[0]:c.bank->animation.sequences[c.sequence-1];
        c.cursor.advance(sequence,milliseconds,false,[&](const auto& frame){queueAudio(c,frame);});
        holdAudio(c);
    }
}
void SceneRenderer::advanceNativeItem(size_t id,double milliseconds,const std::vector<ActorState>& actors,
    const EnemyAnimationView& view,EnemyAnimationPhase phase,bool groundPass) {
    if(!nativeItemPhases_||phase==EnemyAnimationPhase::AwakeUpdate||
       view.playerOnly||
       id>=actors.size()||!actors[id].alive||id>=objectChannels_.size())return;
    for(const auto index:objectChannels_[id]) {
        auto& c=channels_[index];
        if(!c.nativeItem||c.firing||c.nativeInGround!=groundPass)continue;
        if(!c.shadow&&c.nativeSavingPlot&&view.savingPlot!=id)continue;
        // New bindings can reach this phase before the next broad synchronization.
        syncActorChannel(c,actors[id]);
        //431f00..431fa9: Item shadow then body playback, before draw filtering.
        for(const auto& frame:c.pendingAudio)queueAudio(c,frame);
        c.pendingAudio.clear();
        const auto& sequence=c.bank->animation.sequences[c.sequence-1];
        c.cursor.advance(sequence,milliseconds,false,[&](const auto& frame){queueAudio(c,frame);});
        holdAudio(c);
    }
}
void SceneRenderer::advanceEnemyPhase(double milliseconds,const std::vector<ActorState>& actors,
    const EnemyAnimationView& view,EnemyAnimationPhase phase,std::optional<size_t> awakeActor) {
    if(!std::isfinite(milliseconds)||milliseconds<0)return;
    nativeEnemyPhases_=true;
    if(phase==EnemyAnimationPhase::Draw)nativeDrawView_=view;
    if(phase==EnemyAnimationPhase::VisibleUpdate)advanceTerrain(milliseconds,view);
    if(phase==EnemyAnimationPhase::Draw)for(const auto& indices:enemyObjectChannels_)
        for(const auto i:indices)channels_[i].enemyDrawAdmitted=false;
    const auto admitted=[&](size_t id,bool groundPass) {
        advanceNativeItem(id,milliseconds,actors,view,phase,groundPass);
        if(id>=actors.size()||id>=enemyObjectChannels_.size()||enemyObjectChannels_[id].empty()||!actors[id].alive)return;
        auto& c=channels_[enemyObjectChannels_[id].front()];
        if(c.nativeInGround!=groundPass)return;
        if(phase==EnemyAnimationPhase::AwakeUpdate) {if(!c.nativeGraphicUpdate)return;}
        else if(phase==EnemyAnimationPhase::VisibleUpdate) {
            if(c.nativeGraphicUpdate||view.playerOnly)return;
        }
        if(phase==EnemyAnimationPhase::Draw) {
            if(!groundDetails&&c.nativeDetail)return;
            for(const auto index:enemyObjectChannels_[id])channels_[index].enemyDrawAdmitted=true;
        }
        advanceEnemyChannels(id,milliseconds,actors,phase,view.visibilityStep);
    };
    if(phase==EnemyAnimationPhase::AwakeUpdate) {
        if(awakeActor){admitted(*awakeActor,true);admitted(*awakeActor,false);}return;
    }
    if(view.unboundedOracle) {
        for(bool ground:{true,false})for(size_t id=0;id<enemyObjectChannels_.size();++id)admitted(id,ground);
        return;
    }
    if(!view.cells||view.mapW<0||view.mapH<0||view.cells->width!=view.mapW||view.cells->height!=view.mapH)return;
    //433b30 walks Y-major/X-minor cells and then each native head/next chain.
    // In-ground actors are submitted in a separate first pass. These are cell
    // admission bounds, not sprite rectangle intersection/culling margins.
    const auto start=[](int32_t translation,unsigned shift,int margin) {
        if(translation>0)return 0;
        return std::max(0,int((uint32_t(0)-uint32_t(translation))>>shift)-margin);
    };
    const int left=start(view.translationX,6,1),top=start(view.translationY,5,2);
    const int right=std::min(view.mapW,int(view.width>>6)+3+left);
    const int bottom=std::min(view.mapH,int(view.height>>5)+5+top);
    for(bool ground:{true,false})for(int y=top;y<bottom;++y)for(int x=left;x<right;++x) {
        const size_t cell=size_t(x)*size_t(view.mapH)+size_t(y);
        if(cell>=view.cells->heads.size())continue;
        for(size_t id=view.cells->heads[cell];id!=NativeActorCells::Missing;) {
            if(id>=view.cells->next.size())break;
            const auto next=view.cells->next[id];admitted(id,ground);id=next;
        }
    }
}
void SceneRenderer::advance(double milliseconds,const std::vector<ActorState>* actors,double worldTimeMs,const SurpriseState* surprise,std::optional<uint64_t> actorRevision) {
    if(!std::isfinite(milliseconds)||milliseconds<=0)return;
    if(actors && playerObject_ && *playerObject_<actors->size())
        playerTurn_.advance((*actors)[*playerObject_].direction,milliseconds,
                            surprise?surprise->circularRemaining:-1);
    if(actors){syncActors(*actors);rememberActorSync(*actors,actorRevision);}
    orbitTimeMs_=worldTimeMs>=0?worldTimeMs:orbitTimeMs_+milliseconds;
    terrainClockMs_+=milliseconds;
    // 43f8df..43f91b selects mask1 on the three player-only passes. GameWorld
    // increments phase after deciding the pass, so phase1 denotes a full pass.
    const bool playerOnly=surprise && surprise->active(SurpriseKind::TimeWarp) && surprise->timeWarpPhase!=1;
    if(!nativeEnemyPhases_) {EnemyAnimationView view;view.unboundedOracle=true;view.playerOnly=playerOnly;advanceTerrain(milliseconds,view);}
    if(actors && !playerOnly)for(auto& orbit:enemyOrbits_) {
        if(orbit.object>=actors->size())continue;
        const auto& actor=(*actors)[orbit.object];
        if(!actor.alive) {
            orbit.visible=false;
            for(auto& trail:orbit.state.trails)trail.ageMs+=milliseconds;
            std::erase_if(orbit.state.trails,[&](const auto& trail){return trail.ageMs>=orbit.config.trailLifetimeMs;});
            continue;
        }
        const auto bank=banks_.find(bankKey(orbit.config.sprite.path));
        if(bank!=banks_.end() && orbit.config.sprite.sequence.value && orbit.config.sprite.sequence.value<=bank->second.animation.sequences.size()) {
            const auto& sequence=bank->second.animation.sequences[orbit.config.sprite.sequence.value-1];
            if(!orbit.animationInitialized){orbit.cursor.reset(sequence);orbit.animationInitialized=true;}
            orbit.cursor.advance(sequence,milliseconds);
        }
        orbit.fadeClockMs+=milliseconds;
        const float steps=std::floor(orbit.fadeClockMs/14.f);orbit.fadeClockMs-=steps*14.f;
        orbit.visibility=std::clamp(orbit.visibility+(actor.enemyRevealed?steps:-steps),0.f,49.f);
        orbit.visible=orbit.config.timedProtection?actor.timedProtection.active:
            (!orbit.config.onlyWhenVisible || actor.enemyRevealed || orbit.visibility>=1);
        orbit.state.advance(orbit.config,milliseconds,orbitTimeMs_,orbit.parent,orbit.altitude,!actor.enemyRevealed,orbit.visibility);
    }
    if(!actors)refreshChannelScan();
#if VURUS_ACTOR_SYNC_PROFILE
    if(actorSyncProfile::enabled)actorSyncProfile::counts.advanceVisits+=workingChannels_.size();
#endif
    for (const auto channelIndex:workingChannels_) {
        auto& channel=channels_[channelIndex];
        if(channel.skipNewbornAdvance){channel.skipNewbornAdvance=false;continue;}
        if(playerOnly && (!playerObject_ || channel.object!=*playerObject_))continue;
        if(channel.firing)continue;
        if(actors && !channel.shadow && channel.object<actors->size() && (*actors)[channel.object].bodyGraphics && (*actors)[channel.object].bodyGraphics->binding==NativeBodyGraphicsBinding::Present && !(*actors)[channel.object].bodyGraphicsLegacy) {
            channel.pendingAudio.clear();continue;
        }
        if(nativeEnemyPhases_&&nativeItemPhases_&&channel.nativeItem) {
            // Preserve construction/reset audio independently of camera playback.
            for(const auto& frame:channel.pendingAudio)queueAudio(channel,frame);
            channel.pendingAudio.clear();continue;
        }
        if(nativeEnemyPhases_&&channel.nativeEnemy)continue;
        if(channel.derivedInvisible && actors && channel.object<actors->size()) {
            channel.fadeClockMs+=milliseconds;
            const float steps=std::floor(channel.fadeClockMs/14.f);channel.fadeClockMs-=steps*14.f;
            // 432afa falls through432afb/432b01: growing visibility also
            // selects alpha. The49th step is still alpha; the next is opaque.
            if(steps>0)channel.visibilityAlpha=!(*actors)[channel.object].enemyRevealed || channel.visibility+steps<=49;
            channel.visibility=std::clamp(channel.visibility+((*actors)[channel.object].enemyRevealed?steps:-steps),0.f,49.f);
        }
        if(channel.shadowPoseStride>=0){channel.pendingAudio.clear();continue;}
        if(channel.walker && actors && channel.object<actors->size()) {
            const bool run=(*actors)[channel.object].walkingCommand || channel.walkerTurned;
            channel.walkerTurned=false;
            if(!run){channel.pendingAudio.clear();continue;}
        }
        if(channel.spider && actors && channel.object<actors->size() && !(*actors)[channel.object].walkingCommand) {channel.pendingAudio.clear();continue;}
        if(actors && (channel.object>=actors->size() || !(*actors)[channel.object].alive)){channel.pendingAudio.clear();continue;}
        if(channel.helicopter) {
            // 431ae0 case1d selects a heading frame, not a timed rotation.
            // 4334e0's four-bank tilt uses native scalar speed. The port's
            // existing kinematic enemies supply observed displacement per14ms.
            const float speed=std::sqrt(nativeDistanceSquared(channel.flightPosition,channel.flightPrevious))*14.f/milliseconds;
            channel.flightPrevious=channel.flightPosition;
            if(!channel.shadow) {
                channel.flightClockMs+=milliseconds;
                while(channel.flightClockMs>=14) {
                    channel.flightClockMs-=14;
                    if(speed>2){if(channel.flightTilt<25)++channel.flightTilt;}
                    else {const int threshold=speed>1?20:speed>.5f?10:5;
                        channel.flightTilt+=channel.flightTilt>threshold?-1:1;}
                }
            }
            channel.pendingAudio.clear();continue;
        }
        if(channel.bankedEnemy){channel.pendingAudio.clear();continue;} // Held48-pose body bank.
        for(const auto& frame:channel.pendingAudio)queueAudio(channel,frame);
        channel.pendingAudio.clear();
        if(channel.generatorBank && actors && !(*actors)[channel.object].solid)continue;
        const auto& sequence=channel.generatorBank?channel.generatorBank->animation.sequences[0]:channel.bank->animation.sequences[channel.sequence-1];
        if(actors && !channel.shadow && (*actors)[channel.object].explosion && (*actors)[channel.object].explosion->initialized) {
            const auto& authoritative=(*actors)[channel.object].explosion->animation;
            const auto before=channel.cursor.capture();
            // Audio consumes crossed subframes once, but damage-bearing FX use
            // the simulation cursor for their pose (including Time Warp/save).
            if(before.cursor!=authoritative.cursor || before.remaining!=authoritative.remaining || before.flags!=authoritative.flags)
                channel.cursor.advance(sequence,milliseconds,false,[&](const auto& frame){queueAudio(channel,frame);});
            channel.cursor.restore(sequence,authoritative);
            holdAudio(channel);
            continue;
        }
        channel.cursor.advance(sequence,milliseconds,channel.gateRevision!=0,[&](const auto& frame){queueAudio(channel,frame);});
        holdAudio(channel);
        if(channel.gateRevision&&channel.cursor.atLast(sequence))channel.cursor.holdLast(sequence);
    }
}
std::vector<std::pair<size_t,uint64_t>> SceneRenderer::finishedGates(const std::vector<ActorState>& actors) const {
    std::vector<std::pair<size_t,uint64_t>> result;
    for(size_t i=0;i<channelScan_.size();++i) {
        const auto object=channelScan_[i].object;
        if(object>=actors.size()||(!actors[object].gate&&!actors[object].lift))continue;
        const auto& c=channels_[i];
        if(!c.shadow&&
        c.gateRevision==actors[c.object].gateRevision&&c.cursor.atLast(c.bank->animation.sequences[c.sequence-1]))
        result.emplace_back(c.object,c.gateRevision);
    }
    return result;
}
std::vector<size_t> SceneRenderer::finishedEffects(const std::vector<ActorState>& actors) const {
    std::vector<size_t> result;
    for(size_t i=0;i<channelScan_.size();++i) {
        const auto object=channelScan_[i].object;
        if(object>=actors.size()||!actors[object].alive||!actors[object].effect||
           (actors[object].bodyGraphics && actors[object].bodyGraphics->binding==NativeBodyGraphicsBinding::Present && !actors[object].bodyGraphicsLegacy))continue;
        const auto& channel=channels_[i];
        if (!channel.shadow &&
        !channel.cursor.frame(channel.bank->animation.sequences[channel.sequence-1])) result.push_back(channel.object);
    }
    std::stable_sort(result.begin(),result.end(),[&](size_t a,size_t b){return actorRecordOrder(actors[a],a)<actorRecordOrder(actors[b],b);});
    return result;
}
void SceneRenderer::draw(Gfx& gfx,const SceneContent& scene,const Level& level,float cameraX,float cameraY,
                         float playerX,float playerY,int direction,int forcedBitmap,
                         const std::function<void(const ForegroundTile&)>& drawTile,
                         const std::vector<ActorState>* actors,const std::vector<ContentPosition>* previous,float alpha,const SurpriseState* surprise,bool playerOnly,std::optional<uint64_t> actorRevision) {
    textureResidency_.beginDraw();
    if(!playerOnly)playerBodyCenter_.reset();
    // Draw calls never advance player turn state (including the Darkness second pass).
    // A direct debug heading/restore before the next tick uses its exact node.
    const bool tracked=playerTurn_.heading==(direction%24+24)%24;
    const int playerPose=tracked?playerTurn_.pose:2*((direction%24+24)%24);
    const unsigned playerBank=tracked?playerTurn_.bank():25;
    struct Sprite { Bank* bank; uint32_t frame,flags; float intensity=1; float red=0; float opacity=1; float green=0;
        bool timeWarp=false;
    };
    // Only the controlled player's Time Warp submits a rotated quad. Keep its
    // geometry out of every ordinary sprite in crowded scenes.
    std::array<ContentPosition,4> timeWarpCorners{};
    std::vector<Sprite> sprites;
    std::vector<SpriteBounds> bounds;
    if(groundDetails && !playerOnly && !terrainCells_.empty()) {
        //4467de sets world20&4 for initialized graphics: first scenery
        // subframes are already in the baked map.433b30 submits only the timed
        // delta here, at subtype2 and the authored terrain row displacement.
        const auto start=[](int32_t translation,unsigned shift,int margin) {
            return translation>0?0:std::max(0,int((uint32_t(0)-uint32_t(translation))>>shift)-margin);
        };
        const int left=start(static_cast<int32_t>(-cameraX),6,1),top=start(static_cast<int32_t>(-cameraY),5,2);
        const int right=std::min(int(terrainWidth_),int(uint32_t(gfx.viewport().width)>>6)+3+left);
        const int bottom=std::min(int(terrainHeight_),int(uint32_t(gfx.viewport().height)>>5)+5+top);
        for(int y=top;y<bottom;++y)for(int x=left;x<right;++x)for(int slot=3;slot>=0;--slot) {
            const auto id=terrainCells_[size_t(y)*terrainWidth_+x][slot];if(id==SIZE_MAX)continue;
            auto& c=terrain_[id];const auto& sequence=c.bank->animation.sequences[c.sequence-1];
            const auto* frame=c.cursor.frame(sequence);
            if(!frame || frame->frameIndex==GafSubframe::Blank)continue;
            const auto& bitmap=c.bank->animation.frames[frame->frameIndex];
            const bool flipX=frame->flags&0x100,flipY=frame->flags&0x200;
            const float sx=x*64+(flipX?-frame->offsetX:frame->offsetX)+(flipX?bitmap.flippedOriginX:bitmap.originX);
            const float sy=y*32+(flipY?-frame->offsetY:frame->offsetY)+(flipY?bitmap.flippedOriginY:bitmap.originY);
            if(sx+bitmap.image.width<=cameraX || sx>=cameraX+gfx.viewport().width ||
               sy+bitmap.image.height<=cameraY || sy>=cameraY+gfx.viewport().height)continue;
            const int displacement=(c.subtype==3 || c.subtype==15)?0:c.subtype==12?256:128;
            bounds.push_back({(y*128+displacement)|2,sx,sy,sx+bitmap.image.width,sy+bitmap.image.height});
            sprites.push_back({c.bank,frame->frameIndex,frame->flags});
        }
    }
    const bool reuse=actors&&actorsAlreadySynchronized(*actors,actorRevision);
#if VURUS_ACTOR_SYNC_PROFILE
    if(reuse&&actorSyncProfile::enabled)++actorSyncProfile::counts.skips;
#endif
    if(actors && !reuse){syncActors(*actors);rememberActorSync(*actors,actorRevision);}
    // These directional layers have no running cursor and deliberately do not
    // add persisted channels: older checkpoints retain their channel identity.
    struct Submission { Channel* channel; uint32_t overlay=0; float dx=0,dy=0; bool tripleBody=false; };
    std::vector<Submission> ordered;
    if(!actors)refreshChannelScan();
    // Only read-only ordinary body selection may bypass ordered preparation.
    // Special banks/cursor transitions and actor-driven admission stay unchanged.
    const auto bodyOutside=[&](Channel& channel) {
        if(playerOnly || channel.shadow || channel.firing || channel.nativeEnemy ||
           channel.bankedEnemy || channel.walker || channel.spider || channel.helicopter ||
           channel.lift || channel.tank || channel.turretBase || channel.generatorBank ||
           channel.derivedInvisible || (scene.player && channel.object==scene.player->value))return false;
        const auto* actor=actors && channel.object<actors->size()?&(*actors)[channel.object]:nullptr;
        if(actors && !actor)return false;
        if(actor && (!actor->alive || actor->directional || actor->gate || actor->lift ||
                     actor->trailSegment || actor->explosion || actor->turretOnlyBase))return false;
        const auto& object=scene.objects[channel.object];
        if(channel.sequence!=object.sprite.sequence.value || !channel.sequence ||
           channel.sequence>channel.bank->animation.sequences.size())return false;
        auto& bank=*channel.bank;
        if(bank.envelopes.size()<bank.animation.sequences.size())bank.envelopes.resize(bank.animation.sequences.size());
        auto& envelope=bank.envelopes[channel.sequence-1];
        if(!envelope.ready)envelope=spriteSequenceEnvelope(bank.animation,bank.animation.sequences[channel.sequence-1]);
        const auto anchor=previous && channel.object<previous->size() &&
            !(actor && (actor->pooledTrail || actor->reusedRecord) && actor->ageMs==0)
            ?presentationPosition((*previous)[channel.object],object.position,alpha):object.position;
        const float altitude=actor && actor->projectileAltitude?*actor->projectileAltitude:spriteProperties(object).altitude;
        return envelope.outside(anchor.x,anchor.y,cameraX,cameraY,gfx.viewport().width,gfx.viewport().height,altitude);
    };
    std::vector<size_t> nativeChannels,nativeFire;
    const auto* drawView=nativeDrawCells_&&nativeDrawView_&&!nativeDrawView_->unboundedOracle?&*nativeDrawView_:nullptr;
    const bool nativeCells=actors&&drawView&&drawView->cells&&
        drawView->cells->width==level.mapW&&drawView->cells->height==level.mapH;
    if(nativeCells) {
        // The old global preparation also copied World-owned explosion cursors
        // on their birth/rebind frame. Keep that draw-time mirror synchronization
        // outside visibility admission, after finishedEffects has been queried.
        // No timer advancement or World mutation occurs here.
        for(const auto index:pendingDrawBindings_) {
            auto& c=channels_[index];
            if(c.shadow||c.firing||c.object>=actors->size())continue;
            const auto& actor=(*actors)[c.object];
            if(!actor.alive||!actor.explosion||!actor.explosion->initialized)continue;
            const auto& object=scene.objects[c.object];
            const bool player=scene.player&&object.id==*scene.player;
            if((nativeEnemyPhases_&&c.nativeEnemy&&!c.enemyDrawAdmitted)||
               (playerOnly&&!player)||(!playerOnly&&player&&surprise&&surprise->active(SurpriseKind::Darkness))||
               actor.turretOnlyBase||(!groundDetails&&object.flags.detail.value_or(false))||
               (c.derivedInvisible&&object.flags.invisible.value_or(false)&&!actor.enemyRevealed&&c.visibility<1))continue;
            const auto chosen=object.sprite.sequence.value;
            if(!chosen||chosen>c.bank->animation.sequences.size())continue;
            const auto& sequence=c.bank->animation.sequences[chosen-1];
            if(c.sequence!=chosen) {
                c.sequence=chosen;c.pendingAudio.clear();forgetAudio(c);
                c.cursor.reset(sequence,[&](const auto& frame){if(c.pendingAudio.size()<256)c.pendingAudio.push_back(frame);});
            }
            c.cursor.restore(sequence,actor.explosion->animation);
        }
        const auto window=nativeRenderWindow(static_cast<int32_t>(-cameraX),static_cast<int32_t>(-cameraY),
            uint32_t(gfx.viewport().width),uint32_t(gfx.viewport().height),level.mapW,level.mapH);
        for(bool ground:{true,false})visitNativeRenderCells(*drawView->cells,window,[&](size_t id){
            if(id>=actors->size()||!(*actors)[id].alive||id>=objectChannels_.size()||objectChannels_[id].empty())return;
            if(playerOnly&&(!scene.player||id!=scene.player->value))return;
            if(channels_[objectChannels_[id].front()].nativeInGround!=ground)return;
            for(auto index:objectChannels_[id])nativeChannels.push_back(index);
            if(id<objectFireChannels_.size()&&objectFireChannels_[id]!=SIZE_MAX)nativeFire.push_back(objectFireChannels_[id]);
        });
    }
    for(const auto index:pendingDrawBindings_)channels_[index].drawBindingPending=false;
    pendingDrawBindings_.clear();
    const auto& drawChannels=nativeCells?nativeChannels:workingChannels_;
    for(const auto index:drawChannels)if(auto& channel=channels_[index];!channel.firing) {
        if(!nativeCells&&bodyOutside(channel)) {
            continue;
        }
        const bool body=scene.player && channel.object==scene.player->value && !channel.shadow;
        if(body)ordered.push_back({&channel,3});
        // 417b30/431ae0: the Turret's static ground base is sequence25
        // of its primary bank, independent of shadow27 and muzzle26.
        if(channel.turretBase)ordered.push_back({&channel,8});
        if(channel.tank)ordered.push_back({&channel,9}); // Ground hull before elevated gun.
        ordered.push_back({&channel});
        if(channel.generatorBank && actors && channel.object<actors->size() && (*actors)[channel.object].solid)
            ordered.push_back({&channel,6});
        if(body) {
            ordered.push_back({&channel,4}); ordered.push_back({&channel,5});
            if(surprise && surprise->active(SurpriseKind::TimeWarp))ordered.push_back({&channel,7});
        }
    }
    auto insertFire=[&](Channel& channel) {
        if(nativeEnemyPhases_&&channel.object<enemyObjectChannels_.size()&&
            !enemyObjectChannels_[channel.object].empty()&&
            !channels_[enemyObjectChannels_[channel.object].front()].enemyDrawAdmitted)return;
        if(!actors || channel.object>=actors->size() ||
           channel.fireRevision==(*actors)[channel.object].fireRevision)return;
        const bool player=scene.player&&channel.object==scene.player->value;
        const auto& actor=(*actors)[channel.object];
        const int heading=player?direction:actor.weaponHeading();
        const int pose=player?playerPose+1:actor.enemyTurn?
            (channel.sequence==26?actor.enemyTurn->gunPose:actor.enemyTurn->bodyPose):
            (channel.sequence==26?1:2)*((heading%24+24)%24)+1;
        // Native 433710 submits firing under body for poses20..41, otherwise over.
        auto body=std::find_if(ordered.begin(),ordered.end(),[&](const auto& s){return s.channel->object==channel.object&&!s.channel->shadow&&!s.overlay;});
        if(body!=ordered.end() && pose>=20 && pose<=41)ordered.insert(body,{&channel});
        else if(body!=ordered.end())ordered.insert(body+1,{&channel});
    };
    for(const auto index:drawChannels)if(auto& channel=channels_[index];channel.firing)insertFire(channel);
    if(nativeCells){for(auto index:nativeFire)insertFire(enemyFire_[index]);}
    else for(auto& channel:enemyFire_)insertFire(channel);
    if(surprise && scene.player) {
        const auto originals=ordered;
        for(auto submission:originals)if(submission.channel->object==scene.player->value) {
            if(surprise->tripleOffset>0)for(int sign:{-1,1}) {
                const auto offset=tripleFireOffset(direction,surprise->tripleOffset,sign);
                submission.dx=offset.x;submission.dy=offset.y;submission.tripleBody=true;ordered.push_back(submission);
            }
            // 41c390 copies the already invisible actor at the origin.
            // It is not the controlled-player pointer, so 431ae0's invisible
            // non-player gate hides it. Do not submit an opaque parked copy.
        }
    }
    for (auto submission:ordered) {
        auto& channel=*submission.channel;
        if(nativeEnemyPhases_&&channel.nativeEnemy&&!channel.enemyDrawAdmitted)continue;
        const bool playerChannel=scene.player && channel.object==scene.player->value;
        if(playerOnly && !playerChannel)continue;
        if(!playerOnly && playerChannel && surprise && surprise->active(SurpriseKind::Darkness))continue;
        const auto overlay=submission.overlay;
        // insertFire captured this volley before any submissions were drawn.
        // Consume it after all three origins, not after the center flash.
        if (actors && (channel.object>=actors->size() || !(*actors)[channel.object].alive)) continue;
        const auto& object=scene.objects[channel.object];
#if VURUS_SHADOW_VERIFY
        ShadowVerifyScope shadowVerify;
        ++shadowEnvelopeAudit.layers;
#endif
        // Only skip read-only shadow selection. Sequence transitions, leaning,
        // player anchors and special banks retain their complete original path.
        if(channel.shadow && shadows && !playerChannel && !channel.firing && !overlay &&
           !channel.bankedEnemy && !channel.walker && !channel.spider && !channel.helicopter &&
           !channel.lift && !channel.tank && !channel.turretBase && !channel.generatorBank &&
           (!actors || !(*actors)[channel.object].directional || channel.shadowPoseStride>=0)) {
#if VURUS_SHADOW_VERIFY
            ++shadowEnvelopeAudit.policy;
#endif
            if(channel.sequence==object.shadow.sequence.value && channel.sequence>0 &&
               channel.sequence<=channel.bank->animation.sequences.size()) {
#if VURUS_SHADOW_VERIFY
                ++shadowEnvelopeAudit.sequence;
#endif
                auto& bank=*channel.bank;
                if(bank.envelopes.size()<bank.animation.sequences.size())bank.envelopes.resize(bank.animation.sequences.size());
                auto& envelope=bank.envelopes[channel.sequence-1];
                if(!envelope.ready)envelope=spriteSequenceEnvelope(bank.animation,bank.animation.sequences[channel.sequence-1]);
                const auto* spatialActor=actors?&(*actors)[channel.object]:nullptr;
                const auto anchor=previous && channel.object<previous->size() &&
                    !(spatialActor && (spatialActor->pooledTrail || spatialActor->reusedRecord) && spatialActor->ageMs==0)
                    ?presentationPosition((*previous)[channel.object],object.position,alpha):object.position;
#if VURUS_SHADOW_VERIFY
                ++shadowEnvelopeAudit.tested;
#endif
                if(envelope.outside(anchor.x+submission.dx,anchor.y+submission.dy,cameraX,cameraY,gfx.viewport().width,gfx.viewport().height)) {
#if VURUS_SHADOW_VERIFY
                    ++shadowEnvelopeAudit.rejected;
                    // Run the original path instead of skipping it. Every predicted
                    // rejection must emit nothing and leave channel/audio state alone.
                    shadowVerify.finish=[&,count=sprites.size(),cursor=channel.cursor.capture(),seq=channel.sequence,
                        lean=channel.enemyLean,pending=channel.pendingAudio.size(),identity=channel.audioIdentity,
                        cues=audioCues_.size(),starts=queuedAudioStarts_,next=nextAudioIdentity_] {
                        const auto after=channel.cursor.capture();
                        if(sprites.size()!=count || seq!=channel.sequence || lean!=channel.enemyLean ||
                           cursor.cursor!=after.cursor || cursor.flags!=after.flags || cursor.remaining!=after.remaining ||
                           pending!=channel.pendingAudio.size() || identity!=channel.audioIdentity ||
                           cues!=audioCues_.size() || starts!=queuedAudioStarts_ || next!=nextAudioIdentity_) {
                            std::fprintf(stderr,"SHADOW_ENVELOPE_UNSAFE object=%zu\n",channel.object);std::abort();
                        }
                    };
#else
                    continue;
#endif
                }
            }
        }
        const auto& properties=spriteProperties(object);
        // 431ae0 Only-base hides the gun/muzzle, retaining Turret sequence25
        // or Tank's directional ground hull, plus the secondary shadow.
        if(actors && (*actors)[channel.object].turretOnlyBase &&
           (channel.firing || (!channel.shadow && overlay!=(channel.tank?9:8))))continue;
        // Only the authored invisible-enemy channels use enemy reveal state.
        // Drone and Ammo own separate native alpha rules below.
        const bool hidden=channel.derivedInvisible && object.flags.invisible.value_or(false) &&
            (!actors || !(*actors)[channel.object].enemyRevealed);
        if(hidden && (channel.firing || channel.shadow || channel.visibility<1))continue;
        // Native 431ae0 suppresses Is-detail actors with graphics bit0x40.
        // Keep channels advancing while hidden: this is a drawing preference.
        if(!groundDetails && object.flags.detail.value_or(false))continue;
        // The native shadow flag gates the ordinary ground shadow channel,
        // not the auxiliary bank containing firing, reactor and headlight art.
        if(channel.shadow && !shadows)continue;
        const bool player=scene.player && object.id==*scene.player;
        Bank* drawBank=channel.bank;
        float intensity=1,red=0,opacity=1,green=0;
        const bool enemyAlpha=channel.derivedInvisible && !channel.firing && (hidden || channel.visibilityAlpha);
        // Native fade suppresses shadow/auxiliary layers, not the independent
        // muzzle. Player Drone never enters these derived enemy channels.
        if(enemyAlpha && channel.shadow)continue;
        if(enemyAlpha)opacity=std::trunc(channel.visibility*.02f*255.f)/256.f;
        // 431d09 selects operation5 for the copies' vehicle body; callback
        // 4db310 averages keyed source/destination RGB565. Auxiliary fire,
        // shadow and reactor channels keep their authored operations.
        if(submission.tripleBody && !channel.shadow && !channel.firing && !overlay)opacity=.5f;
        if(player && surprise && surprise->active(SurpriseKind::Drone)) {
            if(channel.shadow || overlay || channel.firing)continue;
            // Native 432b18..432b7b: 49-step visibility * .02, with a
            // player floor of .25, quantized to an eight-bit operation3 alpha.
            // Keep the existing 14ms presentation adapter; never fade to zero.
            const float visibility=std::max(0.f,49.f-std::floor((16000.f-surprise->remainingMs)/14.f));
            opacity=std::trunc(std::max(.25f,visibility*.02f)*255.f)/256.f;
        }
        if(player && surprise && surprise->active(SurpriseKind::Motor) && overlay)continue;
        const ActorState* actor=actors?&(*actors)[channel.object]:nullptr;
        if(actor && actor->ammoAlpha) {
            // 4327ef exempts Ammo subtype0x10 from the invisible draw gate.
            // 432b40..432b5f also gives it the player's .25 opacity floor.
            // Match the enabled native alpha path, using the Drone 14ms
            // presentation adapter rather than making visuals frame-rate bound.
            if(channel.shadow || overlay || channel.firing)continue;
            opacity=std::trunc(std::max(.25f,actor->ammoVisibility*.02f)*255.f)/256.f;
        }
        const GafSubframe* pose=nullptr;
        const GafSequence* sequence=nullptr;
        if(overlay==8) {
            sequence=&drawBank->animation.sequences[24];
            if(!sequence->subframes.empty())pose=&sequence->subframes.front();
        } else if(overlay==7) {
            const auto bank=banks_.find(timeWarpBank);if(bank==banks_.end())continue;
            drawBank=&bank->second;sequence=&drawBank->animation.sequences[0];
            // Shipped TIME-WARP is one static looping frame (flags7, duration8).
            // Native advances its cursor while drawing; no frame phase changes
            // the image, so only the running world clock affects presentation.
            pose=&sequence->subframes.front();
        } else if(overlay && overlay!=9) {
            // Native actor+0x1c4 (front light) also requires high ground details.
            if(overlay==5 && !groundDetails)continue;
            if(overlay==5 && actor && !actor->headlightVisible)continue;
            const auto bank=banks_.find(bankKey(object.shadow.path));
            if(bank==banks_.end() || bank->second.animation.sequences.size()<(overlay==6?1:overlay))continue;
            drawBank=&bank->second;
            if(overlay==3 || overlay==4) {
                if(!reactorFx)continue;
                if(!actor || actor->engineGlowMs<=0 || actor->visualThrust<=0)continue;
                const float maximum=properties.thrustMax;
                if(maximum<=0)continue;
                // 432f19 / 4331d8: positive signed thrust, converted to an
                // eight-bit diffuse intensity. The presentation model bounds it.
                intensity=std::clamp(std::trunc(actor->visualThrust*255.f/maximum),0.f,255.f)/255.f;
            }
            sequence=&drawBank->animation.sequences[(overlay==6?1:overlay)-1];
            if(overlay==6)pose=channel.cursor.frame(*sequence);
        } else if(channel.tank && !overlay && actor) {
            sequence=&drawBank->animation.sequences[24];
            const size_t index=actor->enemyTurn?actor->enemyTurn->gunPose-1:actor->direction;
            pose=index<sequence->subframes.size()?&sequence->subframes[index]:nullptr;
        } else if(actor && !channel.shadow && !channel.firing && actor->bodyGraphics && actor->bodyGraphics->binding==NativeBodyGraphicsBinding::Present && !actor->bodyGraphicsLegacy) {
            // Authoritative clocks were advanced in native World/visible phases;
            // pose lookup is const, including after a same-visit heading change.
            const auto& state=*actor->bodyGraphics;
            pose=nativeBodyGraphicsFrame(state,drawBank->animation,actor->direction);
            if(actor->direction>=0 && actor->direction<24) {
                const auto selected=state.slots[actor->direction].sequence;
                if(selected && selected<=drawBank->animation.sequences.size())sequence=&drawBank->animation.sequences[selected-1];
            }
        } else {
            uint32_t chosen=channel.firing?channel.sequence:player && !channel.shadow?playerBank:(channel.shadow?object.shadow.sequence.value:object.sprite.sequence.value);
            if(channel.lift || channel.spider || channel.walker)chosen=channel.sequence;
            if(channel.helicopter)chosen=channel.baseSequence+(channel.shadow?0:channel.flightTilt<5?0:channel.flightTilt<10?1:channel.flightTilt<20?2:3);
            if(actor&&!channel.shadow&&actor->gate)chosen=actor->gateState.sequence.value;
            if(actor&&!channel.shadow&&actor->lift)chosen=actor->liftState.sequence.value;
            if(channel.bankedEnemy && actor) {
                auto turn=actor->enemyTurn.value_or(initializeEnemyTurn({channel.leanSubtype},actor->direction,0));
                turn.lean=channel.enemyLean;
                const int stage=nativeEnemyPhases_&&channel.nativeEnemy?
                    (!turn.lean?-1:channel.leanSubtype==0x16?(std::abs(turn.lean)<6?0:std::abs(turn.lean)<=10?1:2):0):
                    advanceEnemyLean({channel.leanSubtype},turn);
                channel.enemyLean=turn.lean;
                chosen=stage<0?25:channel.leanSubtype==0x16?(turn.lean<0?26:29)+stage:turn.lean<0?26:27;
            } else if(channel.tank && overlay==9 && actor)chosen=1+enemyVisibleBodyPose(*actor);
            else if (actor && !player && !channel.firing && actor->directional && channel.shadowPoseStride<0 && !channel.spider && !channel.walker && !channel.helicopter && drawBank->animation.sequences.size()>=chosen+23)
                chosen+=channel.turretBase?enemyVisibleGunPose(*actor):enemyVisibleBodyPose(*actor);
            if (chosen!=channel.sequence) {
                channel.sequence=chosen;
                channel.pendingAudio.clear();
                forgetAudio(channel);channel.cursor.reset(drawBank->animation.sequences[chosen-1],[&](const auto& frame){if(channel.pendingAudio.size()<256)channel.pendingAudio.push_back(frame);});
            }
            sequence=&drawBank->animation.sequences[channel.sequence-1];
            if(actor && !channel.shadow && actor->explosion && actor->explosion->initialized)
                channel.cursor.restore(*sequence,actor->explosion->animation);
            pose=channel.generatorBank?&sequence->subframes.front():channel.cursor.frame(*sequence);
            if(channel.bankedEnemy && actor) {
                const size_t index=actor->enemyTurn?actor->enemyTurn->bodyPose-1:actor->direction*2;
                pose=index<sequence->subframes.size()?&sequence->subframes[index]:nullptr;
            }
            if(channel.helicopter && actor) {
                const size_t heading=enemyVisibleBodyPose(*actor);
                pose=&sequence->subframes[heading];
            }
            if(channel.shadowPoseStride>=0) {
                const size_t heading=actor?(actor->direction%24+24)%24:0;
                const size_t index=channel.shadowPoseStride && actor && actor->enemyTurn?
                    actor->enemyTurn->bodyPose-1:heading*channel.shadowPoseStride;
                if(index<sequence->subframes.size())pose=&sequence->subframes[index];
            }
            if(channel.spider && !channel.shadow && actor && !actor->walkingCommand) {
                const size_t heading=(actor->direction%24+24)%24;
                pose=&sequence->subframes[heading];
            }
            if(player&&!channel.shadow&&!channel.firing&&!submission.tripleBody&&actor) {
                red=std::floor(std::clamp(actor->hitFlashMs,0.f,500.f)/4.f)/255.f;
                green=actor->shieldPulse;
            }
        }
        if(channel.firing && !player && actor) {
            const size_t index=actor->enemyTurn?(channel.sequence==26?actor->enemyTurn->gunPose:actor->enemyTurn->bodyPose)-1:
                (channel.sequence==26?1:2)*((actor->weaponHeading()%24+24)%24);
            pose=index<sequence->subframes.size()?&sequence->subframes[index]:nullptr;
        }
        if (player && sequence->subframes.size()>=48)
            pose=&sequence->subframes[playerPose];
        const auto position=previous && channel.object<previous->size() && !(actor && (actor->pooledTrail || actor->reusedRecord) && actor->ageMs==0)
            ?presentationPosition((*previous)[channel.object],object.position,alpha):object.position;
        const float groundX=(player?playerX:position.x)+submission.dx;
        const float groundY=(player?playerY:position.y)+submission.dy;
        float altitude=0;
        if (!channel.shadow && actor && actor->projectileAltitude)
            altitude=*actor->projectileAltitude;
        else if (!channel.shadow)altitude=properties.altitude;
        if(overlay==8 || overlay==9)altitude=0; // Turret base / Tank hull stay on ground.
        // 431ae0 case19 lifts Walker artwork32px, preserving its ground depth.
        if(channel.walker)altitude+=32;
        int subtype=channel.shadow?1:0;
        float sortY=groundY;
        if (!channel.shadow) {
            if (object.flags.inGround.value_or(false)) subtype=3;
            else if (channel.lift && !object.shadow.sequence.value) { subtype=1; sortY=std::max(0.f,groundY-32); }
            else if (object.flags.zShadow.value_or(false)) { subtype=1; sortY=std::max(0.f,groundY-64); }
        }
        // 43333b: generator auxiliary layer sorts 0x168 units after the
        // adjusted base key (90 surface rows), so births do not cover its light.
        if(overlay==6){sortY+=90;subtype=0;}
        auto appendPose=[&](const GafSubframe* layer,bool overrideBitmap,float trailX=0,float trailY=0,bool deltaBase=false) {
            if (!layer) return;
            uint32_t frame=layer->frameIndex;
            if (overrideBitmap && player && !channel.shadow && !channel.firing && !overlay && forcedBitmap>=0 &&
                static_cast<size_t>(forcedBitmap)<drawBank->animation.frames.size()) frame=forcedBitmap;
            if (frame==GafSubframe::Blank) return;
            const auto& bitmap=drawBank->animation.frames[frame];
            const bool flipX=layer->flags&0x100,flipY=layer->flags&0x200;
            if(overlay==7) {
                // 433297..43331f: player body anchor/depth, authored additive
                // operation6, scale2, clockwise trunc(clock*360*.001f) degrees.
                // 45eab0/47e710 rotate the scaled offset about that anchor;
                // the bitmap hotspot scales with its rectangle (466f40).
                const auto clock=static_cast<uint32_t>(std::fmod(std::max(0.0,orbitTimeMs_),4294967296.0));
                const int degrees=static_cast<int>(double(clock)*360.0*double(.001f))%360;
                const double angle=degrees*3.14159265358979323846/180.;
                const float cs=std::cos(angle),sn=std::sin(angle);
                const float x=2*((flipX?-layer->offsetX:layer->offsetX)+(flipX?bitmap.flippedOriginX:bitmap.originX));
                const float y=2*((flipY?-layer->offsetY:layer->offsetY)+(flipY?bitmap.flippedOriginY:bitmap.originY));
                Sprite sprite{drawBank,frame,layer->flags};sprite.timeWarp=true;
                timeWarpCorners={{{x,y},{x+2*bitmap.image.width,y},
                    {x+2*bitmap.image.width,y+2*bitmap.image.height},{x,y+2*bitmap.image.height}}};
                SpriteBounds box{static_cast<int>(sortY)*4+subtype,INFINITY,INFINITY,-INFINITY,-INFINITY};
                for(auto& point:timeWarpCorners) {
                    const float rx=point.x*cs-point.y*sn,ry=point.x*sn+point.y*cs;
                    point={groundX+rx,groundY-altitude+ry};
                    box.left=std::min(box.left,point.x);box.right=std::max(box.right,point.x);
                    box.top=std::min(box.top,point.y);box.bottom=std::max(box.bottom,point.y);
                }
                bounds.push_back(box);sprites.push_back(std::move(sprite));return;
            }
            const float left=groundX+trailX+(flipX?-layer->offsetX:layer->offsetX)+(flipX?bitmap.flippedOriginX:bitmap.originX);
            const float top=groundY+trailY-altitude+(flipY?-layer->offsetY:layer->offsetY)+(flipY?bitmap.flippedOriginY:bitmap.originY);
            if(player && !channel.shadow && !channel.firing && !overlay &&
               submission.dx==0 && submission.dy==0 && overrideBitmap)
                playerBodyCenter_=ContentPosition{left+bitmap.image.width/2,top+bitmap.image.height/2};
            // Same viewport rejection as composeObjects, before allocating
            // submissions for offscreen trail samples and sprite layers.
            if(left+bitmap.image.width<=cameraX || left>=cameraX+gfx.viewport().width ||
               top+bitmap.image.height<=cameraY || top>=cameraY+gfx.viewport().height ||
               bitmap.image.width<=0 || bitmap.image.height<=0)return;
            // The base image has its own bounds and can remain visible when
            // the current delta frame is blank/offscreen. Test those bounds
            // before scanning the authored flag, without suppressing the base.
            if(deltaBase && !properties.useDeltaPacking)return;
            bounds.push_back({static_cast<int>(sortY)*4+subtype,left,top,left+bitmap.image.width,top+bitmap.image.height});
            sprites.push_back({drawBank,frame,opacity<1?((layer->flags&~0xffU)|3U):(overlay==3 || overlay==4)?((layer->flags&~0xffU)|7U):(red>0 || green>0)?((layer->flags&~0xffU)|1U):layer->flags,intensity,red,opacity,green});
        };
        // Native 433710 draws subframe1 before the current delta-packed body.
        // The base remains visible even during a timed blank current frame.
        const auto submit=[&](float dx,float dy) {
            if (!channel.shadow && !channel.firing && !overlay &&
                !sequence->subframes.empty()) appendPose(&sequence->subframes.front(),false,dx,dy,true);
            appendPose(pose,true,dx,dy);
        };
        if(actor && actor->trailSegment && !channel.shadow && !channel.firing && !overlay) {
            visitFxTrail(actor->trailSegment->previous,object.position,actor->trailSegment->step,
                [&](ContentPosition p){submit(p.x-object.position.x,p.y-object.position.y);});
        } else submit(0,0);
    }
    if(actors)for(const auto& submission:ordered)if(submission.channel->firing &&
        (!playerOnly || (scene.player && submission.channel->object==scene.player->value)) &&
        !(!playerOnly && scene.player && submission.channel->object==scene.player->value && surprise && surprise->active(SurpriseKind::Darkness)) &&
        submission.channel->object<actors->size())
        submission.channel->fireRevision=(*actors)[submission.channel->object].fireRevision;
    // Native41c180 creates ordinary orbit FX actors;431ae0 keys each by
    // its own groundY. Submit their existing orbit here instead of a HUD
    // overlay so player/other sprites and foreground tiles can occlude them.
    if(!playerOnly && surprise && (surprise->active(SurpriseKind::Banana) || surprise->active(SurpriseKind::Invulnerable) || surprise->active(SurpriseKind::Touch)) && scene.player &&
       actors && scene.player->value<actors->size() && (*actors)[scene.player->value].alive) {
        const bool invulnerable=surprise->active(SurpriseKind::Invulnerable);
        const bool touch=surprise->active(SurpriseKind::Touch);
        for(int group=0;group<(invulnerable?5:touch?3:1);++group) {
            const int slot=invulnerable?group+1:touch?group+6:0;
            const auto& sprite=orbitSprites_[slot];
            auto found=banks_.find(bankKey(sprite.path));
            if(found!=banks_.end() && sprite.sequence.value>0 && sprite.sequence.value<=found->second.animation.sequences.size()) {
                auto& bank=found->second;const auto& sequence=bank.animation.sequences[sprite.sequence.value-1];
                const float elapsed=std::max(0.f,float(surpriseDuration(surprise->kind))-surprise->remainingMs);
                AnimationCursor cursor;cursor.reset(sequence);cursor.advance(sequence,elapsed);
                const int count=invulnerable?3:5;
                const auto& heightConfig=orbitAltitudes_[slot];
                const auto& player=(*actors)[scene.player->value];
                const float parentAltitude=player.projectileAltitude.value_or(
                    spriteProperties(scene.objects[scene.player->value]).orbitAltitude);
                const float retained=surprise->orbitAltitudeInitialized?surprise->orbitAttachmentAltitudes[group]:
                    initialAttachedFxAltitude(heightConfig,parentAltitude);
                // Retain accepted orbit phase; native integer height and
                // parent/attachment inheritance apply independently of depth.
                const float height=advanceAttachedFxAltitude(heightConfig,retained,parentAltitude,elapsed);
                if(const auto* pose=cursor.frame(sequence))for(int j=0;j<count;++j) {
                    const float angle=j*6.28318530718f/count+group*.41887902048f+elapsed*(invulnerable?-.012f:touch?(group==0?.012f:.008f):.0024f);
                    const float radius=invulnerable||touch?32.f:24.f;
                    const auto offset=nativeToSurface(std::cos(angle)*radius,std::sin(angle)*radius);

                    const float x=playerX+offset.x,y=playerY+offset.y;
                    const auto append=[&](const GafSubframe* layer) {
                        if(!layer || layer->frameIndex==GafSubframe::Blank || layer->frameIndex>=bank.animation.frames.size())return;
                        const auto& frame=bank.animation.frames[layer->frameIndex];
                        const bool flipX=layer->flags&0x100,flipY=layer->flags&0x200;
                        const float left=x+(flipX?-layer->offsetX:layer->offsetX)+(flipX?frame.flippedOriginX:frame.originX);
                        const float top=y-height+(flipY?-layer->offsetY:layer->offsetY)+(flipY?frame.flippedOriginY:frame.originY);
                        bounds.push_back({static_cast<int>(y)*4,left,top,left+frame.image.width,top+frame.image.height});
                        sprites.push_back({&bank,layer->frameIndex,layer->flags});
                    };
                    append(pose);
                }
            }
        }
    }
    // Authored ADD FX uses independent ground depth, so rear stars pass
    // behind the vehicle. Trails retain their emission positions while it moves.
    if(!playerOnly && actors)for(const auto& orbit:enemyOrbits_) {
        if(orbit.object>=actors->size())continue;
        auto submit=[&](const SpriteReference& ref,const EnemyOrbitSample& sample,bool trail) {
            const auto found=banks_.find(bankKey(ref.path));
            if(found==banks_.end() || !ref.sequence.value || ref.sequence.value>found->second.animation.sequences.size())return;
            auto& bank=found->second;const auto& sequence=bank.animation.sequences[ref.sequence.value-1];
            AnimationCursor cursor;
            if(trail){cursor.reset(sequence);cursor.advance(sequence,sample.ageMs);}
            const auto* pose=trail?cursor.frame(sequence):orbit.cursor.frame(sequence);
            if(!pose || pose->frameIndex==GafSubframe::Blank || pose->frameIndex>=bank.animation.frames.size())return;
            const auto& frame=bank.animation.frames[pose->frameIndex];
            const bool flipX=pose->flags&0x100,flipY=pose->flags&0x200;
            const float left=sample.position.x+(flipX?-pose->offsetX:pose->offsetX)+(flipX?frame.flippedOriginX:frame.originX);
            const float top=sample.position.y-sample.altitude+(flipY?-pose->offsetY:pose->offsetY)+(flipY?frame.flippedOriginY:frame.originY);
            if(left+frame.image.width<=cameraX || left>=cameraX+gfx.viewport().width ||
               top+frame.image.height<=cameraY || top>=cameraY+gfx.viewport().height)return;
            bounds.push_back({static_cast<int>(sample.position.y)*4,left,top,left+frame.image.width,top+frame.image.height});
            sprites.push_back({&bank,pose->frameIndex,pose->flags});
        };
        if(orbit.visible && (*actors)[orbit.object].alive)for(const auto& particle:orbit.state.particles)submit(orbit.config.sprite,particle,false);
        for(const auto& trail:orbit.state.trails)submit(orbit.config.trailSprite,trail,true);
    }
    const auto commands=composeObjects(level,bounds,cameraX,cameraY,cameraX+gfx.viewport().width,cameraY+gfx.viewport().height);
#if VYRUZ_GLES
    // Upload visible animation frames/filter copies before emitting any sprite
    // draws. Interleaving page writes with reads can force driver backing copies.
    for(const auto& command:commands)if(command.isSprite) {
        const auto& sprite=sprites[command.sprite];
        auto& texture=sprite.bank->textures[sprite.frame];
        const auto& bitmap=sprite.bank->animation.frames[sprite.frame];
        if(!texture)texture=gfx.uploadSpriteTexture(bitmap.image,true);
        const auto opcode=sprite.flags&0xff;
        gfx.prepareSpriteTexture(texture,opcode==1 || opcode==3 || opcode==5,&bitmap.image);
    }
#endif
    for (const auto& command:commands) {
        if (!command.isSprite) { drawTile(command.tile); continue; }
        const auto& sprite=sprites[command.sprite];
        const auto& box=bounds[command.sprite];
        auto& texture=sprite.bank->textures[sprite.frame];
        const auto& bitmap=sprite.bank->animation.frames[sprite.frame];
        if (!texture) texture=gfx.uploadSpriteTexture(bitmap.image,true);
        const auto opcode=sprite.flags&0xff;
        const bool shadow=opcode==8 || opcode==9;
        const bool additive=opcode==6 || opcode==7;
        // The color key belongs to the draw operation, not the bitmap bank.
        if (opcode==7 || opcode==9) { glEnable(GL_ALPHA_TEST); glAlphaFunc(GL_GREATER,0.5f); }
        glBlendFunc(shadow?GL_ZERO:(opcode==0 || additive)?GL_ONE:GL_SRC_ALPHA,
                    shadow?GL_ONE_MINUS_SRC_COLOR:additive?GL_ONE:opcode==0?GL_ZERO:GL_ONE_MINUS_SRC_ALPHA);
        const float u=sprite.flags&0x100?1.f:0.f,v=sprite.flags&0x200?1.f:0.f;
        glColor4f(sprite.intensity,sprite.intensity,sprite.intensity,sprite.opacity);
        if(sprite.red>0 || sprite.green>0) {
            // Native operation1 adds hit red and shield regeneration green to source
            // color, preserving its keyed alpha; this is vehicle tint only.
            const GLfloat specular[]={sprite.red,sprite.green,0,0};
            glTexEnvfv(GL_TEXTURE_ENV,GL_TEXTURE_ENV_COLOR,specular);
            glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_COMBINE);
            glTexEnvi(GL_TEXTURE_ENV,GL_COMBINE_RGB,GL_ADD);
            glTexEnvi(GL_TEXTURE_ENV,GL_SOURCE0_RGB,GL_TEXTURE);
            glTexEnvi(GL_TEXTURE_ENV,GL_SOURCE1_RGB,GL_CONSTANT);
            glTexEnvi(GL_TEXTURE_ENV,GL_OPERAND0_RGB,GL_SRC_COLOR);
            glTexEnvi(GL_TEXTURE_ENV,GL_OPERAND1_RGB,GL_SRC_COLOR);
            glTexEnvi(GL_TEXTURE_ENV,GL_COMBINE_ALPHA,GL_REPLACE);
            glTexEnvi(GL_TEXTURE_ENV,GL_SOURCE0_ALPHA,GL_TEXTURE);
            glTexEnvi(GL_TEXTURE_ENV,GL_OPERAND0_ALPHA,GL_SRC_ALPHA);
        }
        // Alpha-composited world sprites. Additive lights and multiplicative
        // shadows retain their authored operation and unmodified RGB data.
        gfx.beginQuads(texture,opcode==1 || opcode==3 || opcode==5,&bitmap.image);
        if(sprite.timeWarp) {
            const auto& p=timeWarpCorners;
            gfx.quad(p[0].x-cameraX,p[0].y-cameraY,u,v,p[1].x-cameraX,p[1].y-cameraY,1-u,v,
                     p[2].x-cameraX,p[2].y-cameraY,1-u,1-v,p[3].x-cameraX,p[3].y-cameraY,u,1-v);
        } else gfx.quad(box.left-cameraX,box.top-cameraY,u,v,box.right-cameraX,box.top-cameraY,1-u,v,
                       box.right-cameraX,box.bottom-cameraY,1-u,1-v,box.left-cameraX,box.bottom-cameraY,u,1-v);
        gfx.endQuads();
        // Filtering may allocate or upgrade a copy during beginQuads. Count
        // its actual uploaded payload, including copies retained after toggles.
        // The separate count limit still bounds per-texture driver overhead.
        textureResidency_.touch(texture,gfx.spriteTextureBytes(texture));
        glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_MODULATE);
        glColor4f(1,1,1,1);
        glDisable(GL_ALPHA_TEST);
        glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    }
    textureResidency_.trimWithOverhead(32u*1024*1024,1024,[&]{return gfx.spriteAtlasSlackBytes();},[&](auto texture){gfx.deleteTexture(texture);});
}
}
