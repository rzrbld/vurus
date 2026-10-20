#include "game/world.h"
#include "game/ordinary_enemy_ai_native.h"
#include "game/enemy_idle_native.h"
#include "game/enemy_aim.h"
#include "game/properties.h"
#include "game/spider_contact_native.h"
#include "game/native_navigation_filter.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
namespace vyruz {
namespace {
constexpr uint64_t DecorBit=uint64_t(1)<<63,ProjectileBit=uint64_t(1)<<62;
NativeNavPoint nativePoint(ContentPosition p){const auto n=spiderSurfaceToNative(p.x,p.y);return {n.x,n.y};}
bool terminal(const NativeEnemyRouteCore& r){return r.cursor>=r.points.size()||r.cursor+1==r.points.size();}
int heading(NativeNavPoint a,NativeNavPoint b){return spiderContactHeading({b.x-a.x,b.y-a.y});}
bool nextSegment(NativeEnemyRouteCore& r){
    if(r.cursor>=r.points.size())return false;
    const auto from=r.points[r.cursor++];if(r.cursor>=r.points.size())return false;
    r.segmentStart=from;r.segmentEnd=r.points[r.cursor];r.desiredHeading=heading(from,r.segmentEnd);
    return r.desiredHeading!=24;
}
void release(NativeNavGraph& graph,const NativeEnemyRouteCore& route){
    const auto& owned=route.search?route.search->seen:route.ownedPoints;
    for(auto it=owned.rbegin();it!=owned.rend();++it)removeNativeNavigationObject(graph,it->graphNode);
}
void discard(NativeNavGraph& graph,NativeEnemyRouteState& route){
    //42c400 recursively disposes oldest snapshots before their parents.
    for(const auto& saved:route.saved)release(graph,saved);
    route.saved.clear();
}
}
void GameWorld::configureOrdinaryEnemyAI(ActorState& actor,const ObjectPlacement& object,bool legacy){
    auto& config=actor.ordinaryAIConfig;config={};
    const int subtype=actor.enemyTurnConfig.subtype;
    const auto* guardian=attribute(object.properties,"guardian");
    config.enabled=(subtype==0x16||subtype==0x19||subtype==0x1a||
        ((subtype==0x1b||subtype==0x1c||subtype==0x1d||subtype==0x1e||subtype==0x20)&&(actor.brain.seeker||actor.brain.tracker||actor.brain.base!=BrainBase::Fixed)))&&actor.enemy&&
        (!actor.brain.guardian||guardian)&&
        actor.brain.base!=BrainBase::Unsupported;
    if(!config.enabled){actor.ordinaryAI.reset();return;}
    const auto* target=attribute(object.properties,actor.brain.tracker?"tracker":"seeker");
    const auto* patrol=attribute(object.properties,"patrol");
    const auto* random=attribute(object.properties,"random");
    config.targeting={actor.brain.tracker?NativeEnemyActionMode::Tracker:NativeEnemyActionMode::Seeker,
        static_cast<int32_t>(target?propertyNumber(target->children,"Scan-Delay",250):250),subtype,
        actor.brain.base==BrainBase::Straight,false};
    config.base={actor.brain.base==BrainBase::Random?NativeEnemyActionMode::Random:NativeEnemyActionMode::Patrol,
        static_cast<int32_t>(actor.brain.base==BrainBase::Random?
            (random?propertyNumber(random->children,"Delay"):0):
            (patrol?propertyNumber(patrol->children,"Scan-Delay",250):250)),subtype,
        actor.brain.base==BrainBase::Straight,propertyBool(object.properties,"Move-during-path-search")};
    config.sensorRange=static_cast<int32_t>(target?propertyNumber(target->children,"Sensor-Range"):0);
    applyNativeEnemyDetectionDifficulty(config.targeting.scanPeriodMs,config.sensorRange,
        actor.hudEnemy,difficulty_.enemyScanDelay,difficulty_.enemyScanRange);
    config.minimumDistance=static_cast<int32_t>(target?propertyNumber(target->children,"Min-distance"):0);
    config.visionDegrees=static_cast<int32_t>(target?propertyNumber(target->children,"Vision-Angle",360):360);
    config.randomRange=static_cast<int32_t>(random?propertyNumber(random->children,"Range"):0);
    config.canTurnOnPlace=propertyBool(object.properties,"Can-turn-on-place");
    config.reverse=actor.brain.reverse;config.closed=actor.brain.closed;
    config.patrolArrival=subtype==0x19?5:64;
    config.guardian=actor.brain.guardian;
    if(config.guardian){
        const auto pair=[&](const char* key){
            const auto* node=contentProperty(guardian->children,key);
            if(!node||node->values.size()<2)return NativeNavPoint{-1,-1};
            return NativeNavPoint{std::strtof(node->values[0].c_str(),nullptr),std::strtof(node->values[1].c_str(),nullptr)};
        };
        config.guardianHome=resolveNativeEnemyGuardianPoint(pair("Location"),nativePoint(actor.brain.home));
        config.guardianCenter=resolveNativeEnemyGuardianPoint(pair("Center"),nativePoint(actor.brain.center));
        config.guardianRange=static_cast<int32_t>(propertyNumber(guardian->children,"Range"));
    }
    //42c4b0 disposes a permanently removed actor's action storage. Rebuilding
    //derived cache fields on restore must not recreate that deleted storage;
    //temporarily off actors retain it for439140 reactivation.
    if(!actor.alive&&actor.reactivateMs<0){actor.ordinaryAI.reset();return;}
    if(!actor.ordinaryAI){
        actor.ordinaryAI.emplace();
        if(legacy&&actor.brainState.initialized&&actor.brainState.point>=0){
            auto& patrolState=actor.ordinaryAI->patrol;
            patrolState.initialized=true;patrolState.point=actor.brainState.point;patrolState.reverse=actor.brainState.reverse;
        }
        // Old A* indices/frontiers cannot identify native graph-owned points.
        actor.route.clear();actor.routePoint=0;actor.routeSearch.reset();actor.routeGoal.reset();
        actor.randomGoal.reset();actor.randomWaitMs=0;actor.driveSegmentStart.reset();actor.driveSegmentEnd.reset();
    }
}
void GameWorld::clearOrdinaryEnemyRoutes(size_t id){
    if(id>=actors_.size()||!actors_[id].ordinaryAI)return;
    if(spiderNavigation_)for(auto* route:{&actors_[id].ordinaryAI->targeting,&actors_[id].ordinaryAI->base}){
        release(*spiderNavigation_,*route);discard(*spiderNavigation_,*route);
    }
    actors_[id].ordinaryAI.reset();
}
bool GameWorld::advanceOrdinaryEnemyAI(size_t id,float seconds,std::span<const ContentPosition> path){
    auto& actor=actors_[id];const auto& object=scene_->objects[id];
    if(!actor.ordinaryAI)configureOrdinaryEnemyAI(actor,object,true);
    if(!actor.ordinaryAI)return false;
    if(!spiderNavigation_){initializeSpiderNavigation(true);for(auto& shot:projectiles_)updateSpiderProjectileNavigation(shot);}
    auto& state=*actor.ordinaryAI;const auto& config=actor.ordinaryAIConfig;
    if(!actor.nativeMotion){const auto n=nativePoint(object.position);actor.nativeMotion=SpiderMotionHistory{{n.x,n.y},object.position};}
    const auto position=object.position;const auto native=actor.nativeMotion->center;
    const int ms=static_cast<int>(std::floor(elapsedMs_)-std::floor(elapsedMs_-seconds*1000));
    actor.spiderPatrolTurning=false;actor.enemyMoveCommand=0;
    if(actor.enemyTurn){actor.enemyTurn->turnCommand=0;actor.enemyTurn->gunCommand=0;}
    NativeEnemyActionActor commands{state.actorFlags,0,0};
    EnemyMotionCommand motionCommand;EnemyFireAim firing;bool locked=false;
    const auto center=[&](size_t target){
        if(const auto* history=actors_[target].nativeMotionHistory())return history->center;
        return nativePoint(scene_->objects[target].position);
    };
    const auto bboxEnabled=[&](size_t other){
        const auto& a=actors_[other];const auto& o=scene_->objects[other];
        if(!a.alive||nativeDamageSubtype(other)==0x11)return false;
        if((o.flags.invisible.value_or(false)||a.ammoHidden)&&!propertyBool(o.properties,"Keep-bb-when-invisible"))return false;
        if((a.gate||a.lift||!a.generators.empty())&&!a.solid)return false;
        //41a740 disables any zero-size class, including a shape2 point.
        return a.radius>0;
    };
    const auto selectTarget=[&]()->std::optional<size_t>{
        const int range=config.guardian?config.guardianRange:config.sensorRange;
        if(!actor.brain.targetClass||range<0)return {};
        const int cx=int(position.x)/64,cy=int(position.y)/32;
        const auto window=nativeEnemyTargetWindow(range,cx,cy,level_->mapW,level_->mapH);
        const int cone=config.guardian?12:(config.visionDegrees%361)/30;
        const auto detectionCenter=nativeEnemyDetectionCenter(config.guardian,config.guardianCenter,native);
        for(int x=window.left;x<=window.right;++x)
            for(int y=window.top;y<=window.bottom;++y)
                for(size_t other=actorCells_.heads[size_t(x)*size_t(actorCells_.height)+size_t(y)];other!=NativeActorCells::Missing;other=actorCells_.next[other]){
                    const auto& o=scene_->objects[other];const auto& a=actors_[other];
                    if(o.classId!=*actor.brain.targetClass||o.flags.invisible.value_or(false)||a.ammoHidden||a.reactivateMs>=1||!a.alive)continue;
                    if(nativeEnemyTargetDetected(detectionCenter,center(other),range,actor.direction,cone))return other;
                }
        return {};
    };
    const auto randomGoal=[&](NativeEnemyRouteState& route)->std::optional<ContentPosition>{
        if(reuseNativeEnemyRandomGoal(state.random,!route.points.empty()&&terminal(route),route.sampledTarget))return route.sampledTarget;
        return chooseRandomDestination(position,float(config.randomRange),level_->mapW,level_->mapH,
            [&](int low,int high){return nativeGlobalRandom_.inclusive(low,high);},[&](int x,int y){
                const size_t cell=size_t(y)*size_t(level_->mapW)+size_t(x);
                if(cell>=level_->collisionMask.size()||level_->collisionMask[cell]!=0xff)return false;
                for(size_t other=actorCells_.heads[size_t(x)*size_t(actorCells_.height)+size_t(y)];other!=NativeActorCells::Missing;other=actorCells_.next[other])
                    if(bboxEnabled(other))return false;
                const size_t nativeCell=size_t(x)*size_t(actorCells_.height)+size_t(y);
                if(nativeCell<bareAmmoCells_.heads.size())
                    for(auto body=bareAmmoCells_.heads[nativeCell];body!=NativeAmmoCells::Missing;){
                        const auto link=bareAmmoCells_.links.find(body);if(link==bareAmmoCells_.links.end())break;
                        const auto found=bareAmmoIndex_.find(body);
                        if(found!=bareAmmoIndex_.end()&&found->second<projectiles_.size()){
                            const auto& shot=projectiles_[found->second];
                            const auto& cls=scene_->classes[shot.classId.value];
                            const bool keep=shot.phaseConfigReady?shot.keepBoundsWhenHidden:
                                propertyBool(cls.properties,"Keep-bb-when-invisible");
                            if(shot.alive&&shot.radius>0&&(!(shot.phaseHidden||cls.flags.invisible.value_or(false))||keep))return false;
                        }
                        body=link->second.next;
                    }
                return true;
            });
    };
    bool guided=false;
    if(!actor.weapons.empty())if(const auto* ammo=findClass(*scene_,actor.weapons[actor.selectedWeapon].ammo))
        guided=std::any_of(ammo->behaviours.begin(),ammo->behaviours.end(),[](const auto& b){return contentNameIs(b,"tracker");});
    const auto run=[&](NativeEnemyRouteState& route,bool targeting){
        const NativeNavQuery query=[&](NativeNavPoint from,NativeNavPoint to,NativeNavPoint extent,bool overlap){
            return queryNativeNavigation(*spiderNavigation_,from,to,extent,overlap,[&](uint64_t ref){
                if(ref&DecorBit){const auto n=ref&~DecorBit;return n<level_->nativeNavigationDecor.size()&&!level_->nativeNavigationDecor[n].water;}
                //40f920/40fbc0/40ff00 keep Ammo in the shared bounds graph
                //for physical contacts, but exclude kind3/subtype10 from
                //navigation overlap and line queries. Player is subtype18.
                if(ref&ProjectileBit)return nativeNavigationAnimatedActorAdmitted(3,0x10);
                if(ref>=actors_.size()||ref==id||ref==route.target||!bboxEnabled(ref))return false;
                if(!nativeNavigationAnimatedActorAdmitted(3,nativeDamageSubtype(ref)))return false;
                if(nativeDamageSubtype(ref)==0x12)if(const auto* item=attribute(scene_->objects[ref].properties,"item"))
                    if(propertyBool(item->children,"Only-for-player"))return false;
                return true;
            });
        };
        const NativeNavPointFactory create=[&](NativeNavPoint p){return insertNativeNavigationPoint(*spiderNavigation_,p);};
        const bool inCorridor=spiderInsideRouteCorridor(native.x,native.y,route.segmentStart.x,route.segmentStart.y,
            route.segmentEnd.x,route.segmentEnd.y,actor.radius*2,actor.enemyMagnitude);
        bool unchanged=false;
        if(route.target&&*route.target<actors_.size()){
            const auto& target=scene_->objects[*route.target];
            unchanged=nativeEnemyTargetUnchanged(actors_[*route.target].nativeGeneration!=0,true,target.flags.invisible.value_or(false),
                terminal(route),target.position,route.sampledTarget,int(target.position.x)/64,int(target.position.y)/32,route.sampledCellX,route.sampledCellY);
        }
        NativeEnemyActionHooks hooks;
        hooks.beginSearch=[&]{
            std::optional<size_t> target;std::optional<ContentPosition> goal;
            std::optional<NativeEnemyGuardianGoal> guardianGoal;
            if(targeting){
                target=selectTarget();
                if(target)goal=scene_->objects[*target].position;
                else if(config.guardian){guardianGoal=nativeEnemyGuardianGoal(config.guardianHome);goal=guardianGoal->sample;}
            }
            else if(actor.brain.base==BrainBase::Random)goal=randomGoal(route);
            else goal=selectNativeEnemyPatrolGoal(state.patrol,path,position,config.reverse,config.closed,
                !route.points.empty()&&route.cursor>=route.points.size()&&(route.status&1));
            if(!goal)return false;
            route.saved.push_back(static_cast<const NativeEnemyRouteCore&>(route));
            //42dce0 retains selected-route/cursor/shortcuts as references to
            //the saved action until42d520 replaces them.
            route.search.reset();route.ownedPoints.clear();route.target=target;route.sampledTarget=*goal;
            route.sampledCellX=guardianGoal?guardianGoal->cellX:int(goal->x)/64;
            route.sampledCellY=guardianGoal?guardianGoal->cellY:int(goal->y)/32;
            route.search=beginNativeNavigation(native,target?center(*target):guardianGoal?guardianGoal->destination:nativePoint(*goal),{actor.radius,actor.radius},
                targeting?float(config.sensorRange):float(actor.brain.base==BrainBase::Random?config.randomRange:0),create);
            return true;
        };
        hooks.searchWave=[&]{return route.search?int(advanceNativeNavigation(*route.search,query,create,true)):0;};
        hooks.selectAndAdvance=[&]{
            if(!route.search)return false;
            route.points.clear();route.cursor=0; //42d520 clears action+48 first.
            auto selected=selectNativeNavigation(*route.search,query);if(!selected)return false;
            route.points=std::move(selected->points);route.cursor=0;
            // Retain ALL seen points, including discarded alternatives, until
            // this action is replaced/disposed; native42c2c0 owns the array.
            route.ownedPoints=std::move(route.search->seen);route.search.reset();
            return nextSegment(route);
        };
        hooks.rollback=[&]{
            release(*spiderNavigation_,route);
            if(!route.saved.empty()){
                auto saved=std::move(route.saved.back());route.saved.clear();
                static_cast<NativeEnemyRouteCore&>(route)=std::move(saved);
            }else{route.search.reset();route.points.clear();route.ownedPoints.clear();route.cursor=0;}
        };
        hooks.discardSaved=[&]{discard(*spiderNavigation_,route);};
        hooks.cancelBase=[&]{
            discard(*spiderNavigation_,state.base);state.base.clock.searching=state.base.clock.active=false;
            //42d7d0 can dispose the saved owner of a pending action's retained
            //selected-route cache. Native leaves those cached cursor values in
            //freed storage. Preserve the values for42e3d0/reuse, without claiming
            //a live graph identity that cleanup has actually removed.
            for(auto& p:state.base.points)if(p.graphNode!=SIZE_MAX&&
                !spiderNavigation_->objects[p.graphNode].alive)p.graphNode=SIZE_MAX;
        };
        hooks.steer=[&]{
            NativeEnemySteeringInput input;input.position=native;input.endpoint=route.segmentEnd;input.terminal=terminal(route);
            input.arrivalRadius=targeting?config.minimumDistance:actor.brain.base==BrainBase::Patrol?config.patrolArrival:0;
            input.minimumDistance=config.minimumDistance;input.canTurnOnPlace=config.canTurnOnPlace;
            input.orthogonalHeading=actor.enemyTurn?actor.enemyTurn->orthogonalHeading:(actor.direction+3)%24;
            if(targeting&&route.target){input.target=center(*route.target);input.targetTerminalOrGuided=terminal(route)||guided;}
            route.status=nativeEnemySteeringStatus(input);
            const auto turn=[&](bool facing){
                if(actor.enemyTurn&&route.desiredHeading<24){
                    advanceEnemyBodyTurn(actor.enemyTurnConfig,*actor.enemyTurn,route.desiredHeading,facing,ms,actor.enemyMagnitude,actor.speed);
                    advanceEnemyGunTurn(actor.enemyTurnConfig,*actor.enemyTurn,route.desiredHeading,facing,ms);
                    synchronizeEnemyGunTurnHeading(actor.enemyTurnConfig,*actor.enemyTurn);actor.direction=actor.enemyTurn->logicalHeading;
                }
            };
            firing={};
            if(!(route.status&4)){
                const bool more=!(route.status&1)||nextSegment(route);turn(false);
                motionCommand.routeCorrection=enemyRouteCorrection({native.x,native.y},{route.segmentStart.x,route.segmentStart.y},
                    {route.segmentEnd.x,route.segmentEnd.y},config.targeting.subtype==0x19||config.targeting.subtype==0x20);
                if(more)commands.movementCommand=200;
            }else{
                turn(true);
                if(route.status&2){commands.movementCommand=208;actor.contactMotion=false;actor.enemyThrust=std::min(actor.enemyThrust,0.f);}
                if(route.target){route.segmentEnd=center(*route.target);route.desiredHeading=heading(route.segmentStart,route.segmentEnd);}
                NativeEnemyWeaponAimInput aim;
                aim.source=native;aim.targetPresent=route.target.has_value();if(route.target)aim.target=center(*route.target);
                aim.bodyHeading=actor.direction;aim.gunHeading=actor.weaponHeading();aim.desiredHeading=route.desiredHeading;
                aim.subtype=config.targeting.subtype;aim.bodyPose=actor.enemyTurn?actor.enemyTurn->bodyPose:actor.direction+1;
                aim.status=route.status;aim.movementCommand=commands.movementCommand;aim.terminal=terminal(route);
                aim.guided=guided;aim.twoPieces=actor.enemyTurnConfig.twoPieces;aim.canTurnOnPlace=config.canTurnOnPlace;
                aim.weaponPresent=!actor.weapons.empty();if(aim.weaponPresent)aim.range=static_cast<int32_t>(actor.weapons[actor.selectedWeapon].range);
                aim.turnCommand=actor.enemyTurn?actor.enemyTurn->turnCommand:0;
                const auto gate=nativeEnemyWeaponAim(aim);commands.movementCommand=gate.movementCommand;
                firing={gate.admitted,gate.fire};
                if(actor.enemyTurn&&actor.enemyTurnConfig.twoPieces&&route.target)actor.enemyTurn->gunInRange=gate.admitted;
                if(firing.inRange&&commands.movementCommand==200)commands.movementCommand=0;
            }
        };
        const auto result=advanceNativeEnemyAction(route.clock,commands,targeting?config.targeting:config.base,
            {ms,inCorridor,unchanged,route.target==player_},hooks);
        locked|=result.locked;
    };
    if(actor.brain.seeker||actor.brain.tracker)run(state.targeting,true);
    if(nativeEnemyBaseActionAdmitted(commands.flags)){
        if(actor.brain.base==BrainBase::Random||actor.brain.base==BrainBase::Patrol)run(state.base,false);
        else if(actor.brain.base==BrainBase::Fixed){commands.movementCommand=0;if(actor.enemyTurn){actor.enemyTurn->turnCommand=0;actor.enemyTurn->gunCommand=0;}}
        else if(actor.brain.base==BrainBase::Straight)commands.movementCommand=200;
    }
    state.actorFlags=commands.flags;state.lastAction=commands.lastAction;
    actor.enemyMoveCommand=commands.movementCommand;
    actor.brainState.initialized=state.patrol.initialized;actor.brainState.point=state.patrol.point;actor.brainState.reverse=state.patrol.reverse;
    actor.brainState.acquired=state.targeting.target.has_value();actor.brainState.scanRemainingMs=float(state.targeting.clock.scanRemainingMs);
    if(actor.enemyTurn&&(supportsMultipartEnemyIdle(actor.enemyTurnConfig)||
        (actor.enemyTurn->bodyPeriodMs>0&&supportsOnePieceEnemyIdle(actor.enemyTurnConfig)))){
        const auto actual=nativePoint(position),previous=nativePoint(actor.nativeMotion->previousSurface);
        const EnemyIdleTurnAdmission admission{actor.brain.seeker||actor.brain.tracker,
            actual.x,actual.y,previous.x,previous.y,actor.enemyMoveCommand};
        const bool onePiece=onePieceEnemyIdleTurnAdmitted(actor.enemyTurnConfig,*actor.enemyTurn,admission);
        const bool multipart=multipartEnemyIdleTurnAdmitted(actor.enemyTurnConfig,*actor.enemyTurn,admission);
        if(onePiece||multipart){
            const auto random=[&](int low,int high){return nativeWorldRandom_.inclusive(low,high);};
            if(onePiece)advanceOnePieceEnemyIdleTurn(actor.enemyTurnConfig,*actor.enemyTurn,ms,random);
            else advanceMultipartEnemyIdleTurn(actor.enemyTurnConfig,*actor.enemyTurn,ms,random);
            actor.direction=actor.enemyTurn->logicalHeading;
        }
    }
    advanceShieldRegeneration(id);
    motionCommand.heading=actor.direction;
    motionCommand.throttle=actor.enemyMoveCommand==200?EnemyThrottle::Forward:actor.enemyMoveCommand==208?EnemyThrottle::Reverse:EnemyThrottle::Neutral;
    motionCommand.route=state.lastAction==4?EnemyRouteMotion::Random:state.lastAction==12?EnemyRouteMotion::Patrol:EnemyRouteMotion::None;
    EnemyMotionState motion{actor.contactVelocity,actor.enemyThrust,actor.enemyMagnitude,actor.contactMotion};
    const auto step=advanceEnemyMotion(motion,motionCommand,seconds,actor.enemyMotionConfig);
    actor.contactVelocity=motion.velocity;actor.enemyThrust=motion.thrust;actor.enemyMagnitude=motion.magnitude;actor.contactMotion=motion.impactLatch;
    const float factor=14.f/(seconds*1000.f);const ContactVelocity nativeStep{motion.velocity.x/factor,motion.velocity.z/factor};
    actor.contactVelocity={nativeStep.x*factor,nativeStep.z*factor};
    const auto rest=nativeActorRestDecision(id,nativeStep);
    if(rest.rested){actor.enemyMovedLastStep=false;actor.nativeMotion->previousSurface=position;actor.contactVelocity={};if(rest.sleep)sleepNativeActor(id);}
    const auto moved=rest.rested?TerrainDisplacement{}:move(id,step.x,step.y,true,&nativeStep);
    if(moved.ammoAbort)return locked;
    // LAB441d11 clears retained momentum only for Walker/Spider/Tank. Cop
    // and Burner keep factor*displacement for the next438b20 inertia pass.
    const bool smallFamily=config.targeting.subtype==0x19||config.targeting.subtype==0x1f||config.targeting.subtype==0x20;
    if(smallFamily&&!moved.constrained&&!((step.x!=0||step.y!=0)&&lastSpiderSeparated_))actor.contactVelocity={};
    actor.walkingCommand=config.targeting.subtype==0x19&&actor.enemyMoveCommand==200;
    actor.locomoting=nativeDistanceSquared(position,scene_->objects[id].position)>.0001f;
    actor.enemyRevealed=firing.inRange&&actor.visibleWhenFiring;
    fire(id,firing.fire,false,{},firing.inRange);
    return locked;
}
}
