#include "render/animation.h"
#include "render/fx_trail.h"
#include <cstdlib>
#include <iostream>
static void check(bool value,const char* message) {
    if (!value) { std::cerr<<message<<'\n'; std::exit(1); }
}
int main() {
    std::vector<vyruz::ContentPosition> trail;
    const auto emit=[&](auto position){trail.push_back(position);};
    vyruz::visitFxTrail({10,20},{34,20},8,emit);
    check(trail.size()==3 && trail[0].x==34 && trail[1].x==26 && trail[2].x==18 && trail[2].y==20,
        "native trail repeats from endpoint backwards, excluding previous origin");
    trail.clear();vyruz::visitFxTrail({10,20},{17,20},8,emit);
    check(trail.empty(),"native sub-step trail does not draw a fallback missile sprite");
    vyruz::visitFxTrail({10,20},{35.9f,20.9f},8,emit);
    check(trail.size()==3 && trail[0].x==35 && trail[0].y==20 && trail[2].x==18,
        "native trail uses truncating surface interpolation");
    vyruz::GafSequence sequence{{{0,0,0,2},{vyruz::GafSubframe::Blank,0,0,1},{2,0,0,3}},2,1};
    vyruz::AnimationCursor cursor;
    cursor.reset(sequence);
    check(cursor.frame(sequence)->frameIndex==0,"first frame must be immediate");
    cursor.advance(sequence,27);
    check(cursor.frame(sequence)->frameIndex==0,"duration scales by fourteen");
    vyruz::AnimationCursor resumed;
    check(resumed.restore(sequence,cursor.capture()),"restore a partially elapsed frame");
    resumed.advance(sequence,1);
    check(resumed.frame(sequence)->frameIndex==vyruz::GafSubframe::Blank,"restore preserves exact remaining frame time");
    auto invalid=cursor.capture();invalid.cursor=100;
    check(!resumed.restore(sequence,invalid),"reject out-of-range saved frame");
    check(resumed.frame(sequence)->frameIndex==vyruz::GafSubframe::Blank,"failed restore preserves existing cursor");
    cursor.advance(sequence,1);
    check(cursor.frame(sequence)->frameIndex==vyruz::GafSubframe::Blank,"blank keeps timing");
    cursor.advance(sequence,14);
    check(cursor.frame(sequence)->frameIndex==2,"blank must advance");
    cursor.advance(sequence,42);
    check(cursor.frame(sequence)->frameIndex==0,"loop boundary");
    sequence.loopStart=3; cursor.reset(sequence); cursor.advance(sequence,1000);
    check(cursor.frame(sequence)->frameIndex==2,"Lift final-frame hold");
    cursor.holdLast(sequence);
    check(cursor.frame(sequence)->frameIndex==2,"present Lift must already be raised on first draw");
    cursor.advance(sequence,5000);
    check(cursor.frame(sequence)->frameIndex==2,"Lift startup must not replay rise after authored delay");
    sequence.flags=0; cursor.reset(sequence); cursor.advance(sequence,84);
    check(!cursor.frame(sequence),"nonlooping completion must stop");
    sequence.flags=2; for (auto& frame:sequence.subframes) frame.duration=0;
    cursor.reset(sequence);
    check(!cursor.frame(sequence),"zero-duration cycle must terminate safely");
    sequence.flags=8; cursor.reset(sequence);
    check(!cursor.frame(sequence),"authored disabled sequence");
    sequence.flags=2;for(auto& frame:sequence.subframes)frame.duration=1;
    cursor.holdLast(sequence);cursor.advance(sequence,1000);
    check(cursor.atLast(sequence)&&cursor.frame(sequence)->frameIndex==2,"gate startup holds authored last frame");
    sequence.subframes.push_back({3,0,0,1});
    cursor.startGateTransition(sequence);
    check(cursor.frame(sequence)->frameIndex==2,"gate transition begins on third frame");
    cursor.advance(sequence,1000,true);
    check(cursor.atLast(sequence)&&cursor.frame(sequence)->frameIndex==3,"large gate timestep cannot skip completion");
    check(resumed.restore(sequence,cursor.capture()),"restore gate completion with elapsed time overflow");
    resumed.advance(sequence,1000,true);
    check(resumed.atLast(sequence),"restored stopped gate remains at last frame");
    vyruz::AnimationCursor dormant;
    check(resumed.restore(sequence,dormant.capture()),"restore an unstarted firing channel");
    vyruz::GafSequence sounds{{{0,0,0,1,0,1,1},{vyruz::GafSubframe::Blank,0,0,1,0,1,2},{2,0,0,1,0,1,3}},2,1};
    std::vector<uint32_t> entered;
    auto event=[&](const vyruz::GafSubframe& frame){entered.push_back(frame.sample);};
    cursor.reset(sounds,event);
    check(entered==std::vector<uint32_t>{1},"reset emits first frame exactly once");
    cursor.advance(sounds,0,false,event);
    check(entered.size()==1,"zero elapsed time does not replay sample");
    cursor.advance(sounds,42,false,event);
    check(entered==std::vector<uint32_t>({1,2,3,1}),"large step preserves all crossed audio frames including blank and loop");
    entered.clear();
    check(resumed.restore(sounds,cursor.capture()),"restore sounding cursor");
    resumed.advance(sounds,0,false,event);
    check(entered.empty(),"loading does not replay current sample");
    resumed.advance(sounds,14,false,event);
    check(entered==std::vector<uint32_t>{2},"loaded cursor triggers next authored cue");
    entered.clear();
    cursor.startGateTransition(sounds,event);
    check(entered==std::vector<uint32_t>{3},"gate transition start emits its third-frame cue");
    vyruz::GafSequence scenery{{{0,0,0,100},{1,0,0,2},{2,0,0,3}},7,2};
    cursor.startTerrain(scenery);
    check(cursor.capture().cursor==2 && cursor.capture().remaining==28,"terrain skips baked base and starts second subframe immediately");
    cursor.advance(scenery,27);check(cursor.capture().cursor==2,"terrain duration retains native14ms units");
    cursor.advance(scenery,1);check(cursor.capture().cursor==3,"terrain strict zero boundary advances");
    cursor.advance(scenery,42);check(cursor.capture().cursor==2,"terrain loops at authored second subframe without replaying base");
    scenery.flags|=0x40;scenery.loopStart=1;cursor.startTerrain(scenery);
    check(cursor.capture().cursor==2,"reverse terrain seed3 decrements to second subframe");
    scenery.subframes.resize(1);cursor.startTerrain(scenery);
    check(!cursor.frame(scenery) && (cursor.capture().flags&8),"single-frame terrain needs only its baked base");
}
