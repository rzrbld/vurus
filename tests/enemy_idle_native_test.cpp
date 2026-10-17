#include "game/enemy_idle_native.h"
#include "game/native_random.h"
#include <iostream>
#include <stdexcept>
using namespace vyruz;
namespace {void need(bool value,const char* why){if(!value)throw std::runtime_error(why);}}
int main(){try{
    const EnemyTurnConfig cop{0x16,false,true,35,0},burner{0x1a,false,true,42,0};
    for(const auto config:{cop,burner}) {
        auto state=initializeEnemyTurn(config,0,0);state.bodyRemainingMs=50;
        NativeRandomState random{1,2118850534u};int draws=0;
        advanceOnePieceEnemyIdleTurn(config,state,16,[&](int low,int high){++draws;return random.inclusive(low,high);});
        need(draws==2 && state.bodyDesiredPose==14 && random.counter==2118850536u,"aligned event uses original 1..24 target and exactly two world draws");
        need(state.bodyPose==1 && state.bodyRemainingMs==34 && state.turnCommand==203,"positive countdown retains current body while selecting idle goal");
        advanceOnePieceEnemyIdleTurn(config,state,16,[&](int,int){throw std::runtime_error("mid-turn must not draw RNG");return 0;});
        need(state.bodyPose==1 && state.bodyRemainingMs==18,"mid-turn keeps retained goal without random draws");
    }
    // A Cop at pose48 wraps visually while retaining logical node23. The
    // original random endpoint does not expand to48 just because this bank does.
    auto state=initializeEnemyTurn(cop,23,0);state.bodyPose=state.bodyDesiredPose=state.bodyPreviousPose=48;
    NativeRandomState random{1,2118850534u};
    advanceOnePieceEnemyIdleTurn(cop,state,16,[&](int low,int high){return random.inclusive(low,high);});
    need(state.bodyPose==1 && state.bodyPreviousPose==48 && state.bodyDesiredPose==14,"48-pose Cop idle step wraps using the original short arc");
    need(state.logicalHeading==23 && state.bodyRemainingMs==19,"visible wrap does not invent a logical heading update");

    // The pre-step occurs before deriving the turn command. Reaching an odd
    // intermediate endpoint there leaves it stationary rather than snapping it.
    state=initializeEnemyTurn(cop,0,0);state.bodyDesiredPose=2;state.bodyRemainingMs=-50;
    advanceOnePieceEnemyIdleTurn(cop,state,100,[](int,int){throw std::runtime_error("pending endpoint must not draw");return 0;});
    need(state.bodyPose==2 && state.bodyDesiredPose==2 && state.bodyPreviousPose==1,"negative countdown pre-step reaches intermediate endpoint");
    need(state.turnCommand==0 && state.logicalHeading==0 && state.bodyRemainingMs==25,"aligned intermediate endpoint clears command and retains ring");

    // Catch-up advances the logical ring by ONE node and maps it to two body
    // poses, unlike Spider's six-node endpoint transition.
    state=initializeEnemyTurn(cop,0,0);state.bodyDesiredPose=2;
    advanceOnePieceEnemyIdleTurn(cop,state,100,[](int,int){throw std::runtime_error("pending endpoint must not draw");return 0;});
    need(state.bodyPose==4 && state.bodyDesiredPose==5 && state.bodyPreviousPose==3,"long idle update preserves native repeated-step endpoint ordering");
    need(state.logicalHeading==2 && state.orthogonalHeading==5 && state.bodyRemainingMs==5,"ordinary endpoint advances one heading per continuation");

    // A not-due AI pass leaves the desired pose and cleared command unchanged;
    // the subsequent admitted idle pass shares and decrements its countdown.
    state=initializeEnemyTurn(cop,0,0);state.bodyRemainingMs=35;state.turnCommand=0;
    advanceEnemyBodyTurn(cop,state,4,true,16,0,100);
    EnemyIdleTurnAdmission admission{true,1,2,1,2,0};
    need(state.bodyRemainingMs==19 && onePieceEnemyIdleTurnAdmitted(cop,state,admission),"not-due body AI can admit native idle controller");
    advanceOnePieceEnemyIdleTurn(cop,state,16,[](int low,int high){need(low==1 && high==150,"non-event draw bounds");return 1;});
    need(state.bodyPose==1 && state.bodyRemainingMs==3,"AI and idle preserve their shared countdown ordering");

    auto input=admission;input.currentX+=.001f;need(!onePieceEnemyIdleTurnAdmitted(cop,state,input),"actual position history suppresses idle");
    input=admission;input.movementCommand=200;need(!onePieceEnemyIdleTurnAdmitted(cop,state,input),"movement command suppresses idle");
    input=admission;input.seekerOrTracker=false;need(!onePieceEnemyIdleTurnAdmitted(cop,state,input),"non-targeting actor cannot idle-turn through this branch");
    state.turnCommand=203;need(!onePieceEnemyIdleTurnAdmitted(cop,state,admission),"AI body turn command suppresses idle");state.turnCommand=0;
    auto config=cop;config.canTurnOnPlace=false;need(!onePieceEnemyIdleTurnAdmitted(config,state,admission),"class Can-turn-on-place gates idle");
    config=cop;config.subtype=0x18;need(!onePieceEnemyIdleTurnAdmitted(config,state,admission),"accepted player controller is excluded");
    config=cop;config.subtype=0x1f;need(!onePieceEnemyIdleTurnAdmitted(config,state,admission),"Spider retains its separate controller");
    config=cop;config.twoPieces=true;need(!onePieceEnemyIdleTurnAdmitted(config,state,admission),"multipart gun idle is outside this bounded helper");
    for(const int subtype:{0x19,0x1b,0x1c,0x1d}) {
        const EnemyTurnConfig onePiece{subtype,false,true,42,0};
        state=initializeEnemyTurn(onePiece,0,0);state.bodyDesiredPose=2;
        advanceOnePieceEnemyIdleTurn(onePiece,state,100,[](int,int){throw std::runtime_error("pending ordinary body must not draw");return 0;});
        need(state.bodyPose==3 && state.bodyDesiredPose==3 && state.logicalHeading==2 && state.bodyRemainingMs==26,"24-pose ordinary idle endpoint advances one ring node with multiplier1");
        state=initializeEnemyTurn(onePiece,0,0);state.bodyRemainingMs=50;random={1,2118850534u};
        advanceOnePieceEnemyIdleTurn(onePiece,state,16,[&](int low,int high){return random.inclusive(low,high);});
        need(state.bodyPose==1 && state.bodyDesiredPose==14 && state.bodyRemainingMs==34,"24-pose ordinary random idle retains original1..24 endpoint range and not-due body clock");
        need(onePieceEnemyIdleTurnAdmitted(onePiece,state,admission)==false,"new idle command prevents a second independent admission");
        state.turnCommand=0;need(onePieceEnemyIdleTurnAdmitted(onePiece,state,admission),"all four other ordinary families share native idle admission");
    }
    for(const int subtype:{0x1e,0x20}) {
        const EnemyTurnConfig multipart{subtype,true,true,subtype==0x1e?60:150,subtype==0x1e?75:50};
        state=initializeEnemyTurn(multipart,0,0);state.gunRemainingMs=50;
        random={1,2118850534u};
        advanceMultipartEnemyIdleTurn(multipart,state,16,[&](int low,int high){return random.inclusive(low,high);});
        need(state.gunPose==24 && state.gunDesiredPose==14 && state.gunRemainingMs==34,"aligned multipart gun steps before a positive countdown return");
        need(state.gunLogicalHeading==0 && state.turnCommand==0 && state.gunCommand==0,"multipart visual gun changes without refreshing its ring or inventing body command");
        state=initializeEnemyTurn(multipart,0,1);state.bodyDesiredPose=7;state.gunDesiredPose=14;
        state.gunRemainingMs=50;state.gunPreviousCommand=203;
        advanceMultipartEnemyIdleTurn(multipart,state,100,[](int,int){throw std::runtime_error("pending gun must not draw");return 0;});
        need(state.gunRemainingMs==-50 && state.gunPose==2 && state.bodyPose==1,"positive multipart countdown can become negative without advancing poses");
        state.turnCommand=state.gunCommand=0;
        advanceMultipartEnemyIdleTurn(multipart,state,16,[](int,int){throw std::runtime_error("pending gun must not draw");return 0;});
        need(state.bodyPose==2 && state.gunPose==4 && state.gunPreviousPose==3,"negative multipart countdown pre-steps BODY and carries gun before due gun step");
        need(state.logicalHeading==0 && state.gunLogicalHeading==1,"idle body/carry steps do not silently synchronize logical rings");
        need(state.gunRemainingMs==multipart.authoredBodyPeriodMs-50,"out-of-range gun idle replenishes BODY period");
        synchronizeEnemyGunTurnHeading(multipart,state);
        need(state.gunLogicalHeading==3,"next route-facing pass explicitly refreshes gun logical ring from pose");

        state=initializeEnemyTurn(multipart,0,0);state.bodyDesiredPose=13;state.gunRemainingMs=10;
        state.gunPreviousCommand=0;state.gunInRange=true;
        advanceMultipartEnemyIdleTurn(multipart,state,16,[](int low,int){return low;});
        need(state.gunRemainingMs==multipart.authoredTurretPeriodMs,"changed gun command resets clock before adding retained in-range GUN period");
        need(state.gunLogicalHeading==1 && state.gunDesiredPose==2 && state.gunPose==1,"aligned nonzero body command advances gun ring endpoint without a same-pass step");
        state.turnCommand=0;need(multipartEnemyIdleTurnAdmitted(multipart,state,admission),"multipart admission shares original actual-history/command gates");
        input=admission;input.previousZ+=1;need(!multipartEnemyIdleTurnAdmitted(multipart,state,input),"multipart idle requires unchanged actual history");
        input=admission;input.movementCommand=208;need(!multipartEnemyIdleTurnAdmitted(multipart,state,input),"multipart reverse command suppresses idle");
    }
    std::cout<<"All ordinary-family native idle RNG, 48-pose wrap, independent logical ring, shared AI countdown and scoped admission passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
