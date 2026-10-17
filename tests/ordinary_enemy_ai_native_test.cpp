#include "game/ordinary_enemy_ai_native.h"
#include "game/ordinary_enemy_ai_validation.h"
#include <array>
#include <iostream>
#include <stdexcept>
using namespace vyruz;
static void need(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
int main(){try{
    for(uint32_t bits=0;bits<4;++bits)need(nativeEnemyBaseActionAdmitted(bits)==(bits!=1),"Seeker/base admission retains the rejected-route bit");
    NativeEnemyActionClock clock{true,false,-1,0};NativeEnemyActionActor actor{0,0,0};
    NativeEnemyActionConfig seeker{NativeEnemyActionMode::Seeker,1000,0x19,false,true};
    NativeEnemyActionHooks hooks;int waves=0,rollbacks=0;
    hooks.searchWave=[&]{++waves;return -2;};hooks.rollback=[&]{++rollbacks;clock={true,true,29,77};};
    advanceNativeEnemyAction(clock,actor,seeker,{17},hooks);
    need(waves==1&&rollbacks==1&&clock.searching&&clock.active&&clock.scanRemainingMs==29&&clock.pathRemainingMs==77,"Seeker pending fails and restores the complete saved clock after its increment");
    clock={true,false,-1,0};hooks.searchWave=[&]{return 1;};hooks.selectAndAdvance=[] {return false;};
    advanceNativeEnemyAction(clock,actor,seeker,{17},hooks);
    need(!clock.searching&&clock.active&&clock.scanRemainingMs==1029&&clock.pathRemainingMs==77&&nativeEnemyBaseActionAdmitted(actor.flags),"Rejected completed route increments restored clock and admits base action");
    clock={true,false,-1,0};actor={};auto tracker=seeker;tracker.mode=NativeEnemyActionMode::Tracker;hooks.searchWave=[] {return -2;};
    advanceNativeEnemyAction(clock,actor,tracker,{17},hooks);
    need(clock.searching&&!clock.active&&clock.scanRemainingMs==-18&&clock.pathRemainingMs==0,"Tracker preserves pending route with one native wave per visit");
    clock={false,true,2,0};actor={1,0,7};hooks.steer=[&]{actor.movementCommand=200;};
    advanceNativeEnemyAction(clock,actor,seeker,{17,true,true,true},hooks);
    need(clock.scanRemainingMs==-15&&actor.movementCommand==200,"Positive scan is tested before elapsed subtraction, allowing this visit to steer");
    clock={false,true,0,0};actor={1,0,7};
    advanceNativeEnemyAction(clock,actor,seeker,{17,false},hooks);
    need(actor.lastAction==7&&!clock.active&&actor.movementCommand==0,"Corridor escape skips default command and last-action tail");
    NativeEnemyRandomState random;need(reuseNativeEnemyRandomGoal(random,false,{64,32})&&random.reuseCount==1,"Random retries previous interior goal once");
    need(!reuseNativeEnemyRandomGoal(random,false,{64,32})&&random.reuseCount==0,"Second reuse attempt selects a new random goal");
    random.reuseCount=1;need(!reuseNativeEnemyRandomGoal(random,true,{64,32})&&random.reuseCount==1,"Terminal Random action leaves attribute counter unchanged");
    need(nativeEnemyTargetUnchanged(true,true,true,false,{65,32},{64,32},1,1,1,1),"Interior target validation compares cells and ignores invisibility");
    need(!nativeEnemyTargetUnchanged(true,true,true,true,{64,32},{64,32},1,1,1,1),"Terminal target validation rejects invisible actor");
    need(resolveNativeEnemyGuardianPoint({100,-1},{1500,-500})==NativeNavPoint{1500,-500},"Guardian negative pair preserves even a negative native birth component");
    need(nativeEnemyDetectionCenter(true,{1500,500},{2000,700})==NativeNavPoint{1500,500},"Positive Guardian center stays fixed after actor movement");
    need(nativeEnemyDetectionCenter(true,{1500,-500},{2000,700})==NativeNavPoint{2000,700},"Negative resolved Guardian center follows current actor");
    const auto home=nativeEnemyGuardianGoal({1500,500});
    need(home.sample.x==1500&&home.sample.y==500&&home.cellX==23&&home.cellY==15,"Guardian fallback retains native Location as sampled coordinates");
    const auto negativeHome=nativeEnemyGuardianGoal({1500,-500});
    need(negativeHome.cellY==134217712,"Guardian fallback cell conversion uses native logical shift even for negative native Location");
    need(nativeEnemyTargetDetected({1500,500},{1800,500},300,6,0)&&!nativeEnemyTargetDetected({1500,500},{1800.001f,500},300,6,0),"Original target acquisition includes radius boundary without a float-rounded squared-distance spill");
    need(nativeEnemyTargetDetected({1500,500},{1500,500},0,0,0)&&!nativeEnemyTargetDetected({1500,500},{1500,500},0,1,0),"Original zero-distance targeting retains4075c0 unordered conversion heading0");
    NativeEnemyWeaponAimInput aim;aim.source={1500,500};aim.target={1800.001f,500};aim.bodyHeading=aim.gunHeading=aim.desiredHeading=6;aim.range=300;aim.targetPresent=aim.terminal=aim.weaponPresent=true;
    need(nativeEnemyWeaponAim(aim).admitted&&nativeEnemyWeaponAim(aim).fire,"Native weapon range truncates squared distance before comparison");
    aim.ready=false;need(nativeEnemyWeaponAim(aim).admitted&&!nativeEnemyWeaponAim(aim).fire,"Unready weapon still retains admitted range");aim.ready=true;aim.turnCommand=203;need(nativeEnemyWeaponAim(aim).admitted&&!nativeEnemyWeaponAim(aim).fire,"Body turn suppresses fire without clearing native range");aim.turnCommand=0;
    aim.terminal=false;need(!nativeEnemyWeaponAim(aim).admitted,"Ordinary ammunition waits for terminal route");aim.guided=true;aim.desiredHeading=0;need(nativeEnemyWeaponAim(aim).admitted&&nativeEnemyWeaponAim(aim).fire,"Guided ammunition bypasses terminal and desired-route heading gates");
    aim.guided=false;aim.terminal=true;aim.status=4;aim.canTurnOnPlace=false;aim.movementCommand=208;need(!nativeEnemyWeaponAim(aim).admitted&&nativeEnemyWeaponAim(aim).movementCommand==200,"Non-turning native body mismatch requests forward motion before weapon range");
    aim.twoPieces=true;need(nativeEnemyWeaponAim(aim).movementCommand==208,"Multipart gun mismatch does not request hull forward motion");
    std::array<ContentPosition,3> path={ContentPosition{0,0},ContentPosition{64,32},ContentPosition{128,64}};NativeEnemyPatrolState patrol;
    auto goal=selectNativeEnemyPatrolGoal(patrol,path,{32.9f,16.9f},false,false,false);
    need(goal&&goal->x==64&&patrol.point==1,"Patrol nearest-point tie uses descending authored order and integer position");
    selectNativeEnemyPatrolGoal(patrol,path,{},false,false,true);selectNativeEnemyPatrolGoal(patrol,path,{},false,false,true);
    need(patrol.point==1&&patrol.reverse,"Open Patrol reverses at endpoint without waypoint drift");
    std::optional<NativeNavGraph> graph{NativeNavGraph{}};OrdinaryEnemyNativeState state;
    const auto core=[&](float x){NativeEnemyRouteCore c;auto a=insertNativeNavigationPoint(*graph,{x,0});auto b=insertNativeNavigationPoint(*graph,{x+1,0});c.points=c.ownedPoints={a,b};c.cursor=1;return c;};
    state.targeting=NativeEnemyRouteState{};static_cast<NativeEnemyRouteCore&>(state.targeting)=core(0);
    for(int i=0;i<11;++i)state.targeting.saved.push_back(core(float(i*10+10)));
    std::set<size_t> claimed;need(validOrdinaryEnemyAI(state,2,graph,claimed),"Native warns after10 saved actions but permits an11-entry chain");
    auto alias=state;auto& route=alias.targeting;route.saved.push_back(static_cast<const NativeEnemyRouteCore&>(route));
    route.search=beginNativeNavigation({1000,0},{1100,0},{1,1},0,[&](auto p){return insertNativeNavigationPoint(*graph,p);});route.ownedPoints.clear();route.clock.searching=true;
    claimed.clear();need(validOrdinaryEnemyAI(alias,2,graph,claimed),"Pending action selected-route references may alias saved ownership");
    auto invalid=alias;invalid.base.ownedPoints=invalid.targeting.saved.back().ownedPoints;invalid.base.points=invalid.base.ownedPoints;invalid.base.cursor=1;
    claimed={999999};const auto before=claimed;need(!validOrdinaryEnemyAI(invalid,2,graph,claimed)&&claimed==before,"Cross-action duplicate ownership rejects atomically");
    auto detached=alias;auto& cached=detached.targeting;
    for(auto p:cached.saved.back().ownedPoints)removeNativeNavigationObject(*graph,p.graphNode);
    cached.saved.pop_back();cached.clock.searching=false;
    for(auto& p:cached.points)p.graphNode=SIZE_MAX;
    claimed.clear();need(validOrdinaryEnemyAI(detached,2,graph,claimed),"Cancelled replacement preserves selected cache values after saved owner disposal without a live graph claim");
    std::cout<<"Native coupled action, target, Patrol, Random and ownership boundaries passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
