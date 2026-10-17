#pragma once
#include "game/properties.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace vyruz {
// Native attached FX subset used by authored enemy ADD FX records. Event
// do-fx is unconditional (41c0b0); visibility is independently gated437b00.
struct EnemyOrbitConfig {
    SpriteReference sprite{},trailSprite{};
    int count=0;
    float radius=0,angularSpeed=0,phase=0,verticalAmplitude=0,verticalPulsation=0,verticalPhase=0;
    bool onlyWhenVisible=false,timedProtection=false;
    float altitude=0;
    float trailDelayMs=0,trailRepeatMs=0,trailLifetimeMs=0;
};
inline std::vector<EnemyOrbitConfig> enemyOrbitConfigs(const SceneContent& scene,const ObjectPlacement& object) {
    std::vector<EnemyOrbitConfig> result;
    for(const auto& add:object.properties) {
        if(!contentNameIs(add.key,"add") || add.arguments.empty() || !contentNameIs(add.arguments.front(),"fx"))continue;
        const auto* cls=findClass(scene,propertyText(add.children,"Name"));
        if(!cls)continue;
        const auto* fx=attribute(cls->properties,"fx");
        if(!fx || !propertyBool(fx->children,"circular"))continue;
        const auto event=propertyText(fx->children,"check-event");
        const bool timedProtection=contentNameIs(event,"bonus-invulnerability") &&
            propertyNumber(fx->children,"on-timeout")>0 && propertyNumber(fx->children,"off-timeout")>0;
        if(!contentNameIs(event,"do-fx") && !timedProtection)continue;
        EnemyOrbitConfig c;c.sprite=cls->sprite;c.timedProtection=timedProtection;
        c.altitude=propertyNumber(cls->properties,"Altitude");
        c.count=std::max(0,int(propertyNumber(fx->children,"count",1)));
        c.radius=propertyNumber(fx->children,"ray");c.angularSpeed=propertyNumber(fx->children,"angular-speed");
        c.phase=propertyNumber(fx->children,"phase");c.verticalAmplitude=propertyNumber(fx->children,"vertical-amplitude");
        c.verticalPulsation=propertyNumber(fx->children,"vertical-pulsation");c.verticalPhase=propertyNumber(fx->children,"vertical-phase");
        c.onlyWhenVisible=propertyBool(fx->children,"only-when-visible");
        for(const auto& create:cls->properties)if(contentNameIs(create.key,"create")) {
            const auto* trail=findClass(scene,propertyText(create.children,"Name"));
            if(!trail)continue;
            c.trailSprite=trail->sprite;c.trailDelayMs=propertyNumber(create.children,"Delay");
            c.trailRepeatMs=propertyNumber(create.children,"Repeat-timeout");
            c.trailLifetimeMs=propertyNumber(trail->properties,"Life-Time");break;
        }
        result.push_back(c);
    }
    return result;
}
struct EnemyOrbitSample { ContentPosition position{};float altitude=0,ageMs=0; };
inline EnemyOrbitSample enemyOrbitSample(const EnemyOrbitConfig& c,int particle,double worldTimeMs,ContentPosition parent,float parentAltitude) {
    const double angle=worldTimeMs*c.angularSpeed+c.phase+particle*6.283185307179586/c.count;
    const double sine=std::sin(angle)*c.radius,cosine=std::cos(angle)*c.radius;
    return {{parent.x+float((sine-cosine)*.8944271802902222),parent.y+float((sine+cosine)*.4472135901451111)},
        float(int(parentAltitude+(c.verticalAmplitude!=0?c.verticalAmplitude*std::sin(worldTimeMs*c.verticalPulsation+c.verticalPhase):c.altitude))),0};
}
struct EnemyOrbitState {
    std::vector<EnemyOrbitSample> particles,trails;
    double ageMs=0,nextTrailMs=0;
    bool initialized=false,trailDone=false;
    void reset(const EnemyOrbitConfig& c,ContentPosition parent,float altitude) {
        particles.assign(c.count,{parent,altitude+c.altitude,0});trails.clear();ageMs=0;
        nextTrailMs=c.trailDelayMs;initialized=true;trailDone=false;
        // 413800 creates delay0 products before41c180 binds parent/altitude.
        // Construction ignores the later only-when-visible gate.
        if(nextTrailMs<=0 && c.trailLifetimeMs>0 && !c.trailSprite.path.empty()) {
            for(int j=0;j<c.count;++j)trails.push_back({parent,c.altitude,0});
            if(c.trailRepeatMs>0)nextTrailMs+=c.trailRepeatMs;else trailDone=true;
        }
    }
    // Call once per presentation update, not per draw; samples retain actual
    // parent movement instead of reconstructing trails around its new center.
    void advance(const EnemyOrbitConfig& c,double milliseconds,double worldTimeMs,ContentPosition parent,float altitude,bool runtimeHidden,float visibilityCounter) {
        if(!initialized)reset(c,parent,altitude);
        for(auto& trail:trails)trail.ageMs+=float(milliseconds);
        std::erase_if(trails,[&](const auto& trail){return trail.ageMs>=c.trailLifetimeMs;});
        ageMs+=milliseconds;
        const bool visible=!c.onlyWhenVisible || !runtimeHidden || visibilityCounter>=1;
        if(!trailDone && c.trailLifetimeMs>0 && !c.trailSprite.path.empty() && ageMs>=nextTrailMs) {
            if(visible)for(const auto& particle:particles)trails.push_back({particle.position,c.onlyWhenVisible?altitude:particle.altitude,0});
            if(c.trailRepeatMs>0)nextTrailMs+=c.trailRepeatMs;else trailDone=true;
        }
        for(int j=0;j<c.count;++j) {
            const float retainedAltitude=c.onlyWhenVisible?altitude:particles[j].altitude;
            particles[j]=enemyOrbitSample(c,j,worldTimeMs,parent,altitude);
            if(c.verticalAmplitude==0)particles[j].altitude=retainedAltitude;
            particles[j].ageMs=float(ageMs);
        }
    }
};
}
