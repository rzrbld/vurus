#include "game/combat.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cmath>
#include <limits>

int main() {
    using namespace vyruz;
    // PE50d440 is already projected/scaled: Start-distance250 is a muzzle
    // offset of31.62275 surface pixels at heading9, not250 pixels.
    const auto right=projectileStartOffset(9,250),left=projectileStartOffset(21,250);
    assert(std::abs(right.x-31.62275f)<.00001f&&right.y==0);
    assert(left.x==-right.x&&left.y==0);
    const auto down=projectileStartOffset(3,250),up=projectileStartOffset(15,250);
    assert(down.x==0&&std::abs(down.y-15.8114f)<.00001f&&up.y==-down.y);
    for(int direction=0;direction<24;++direction){
        const auto offset=projectileStartOffset(direction,250);
        const auto opposite=projectileStartOffset((direction+12)%24,250);
        assert(offset.x==-opposite.x&&offset.y==-opposite.y);
        assert(projectileStartOffset(direction,0).x==0&&projectileStartOffset(direction,0).y==0);
    }
    assert(projectileStartOffset(24,250).x==0&&projectileStartOffset(-1,250).y==0);
    assert(projectileStartOffset(9,std::numeric_limits<float>::infinity()).x==0);
    const WeaponConfig gatling{98, 20, true, true, false};
    WeaponState state;
    uint32_t ammo = 1000;
    auto volley = fireWeapon(gatling, state, true, 0, ammo);
    assert(volley.size() == 2 && volley[0].lateralOffset == 10 && volley[1].lateralOffset == -10);
    assert(ammo == 998);
    assert(fireWeapon(gatling, state, true, 97, ammo).empty());
    assert(fireWeapon(gatling, state, false, 98, ammo).empty());
    assert(fireWeapon(gatling, state, true, 98, ammo).size() == 2);
    assert(fireWeapon(gatling, state, true, 100000, ammo).size() == 2); // no catch-up burst
    assert(fireWeapon(gatling, state, true, 100000, ammo).empty());
    assert(fireWeapon(gatling, state, true, 1, ammo).empty()); // backwards clock
    assert(ammo == 994);
    ammo = 1;
    volley = fireWeapon(gatling, state, true, 100098, ammo);
    assert(volley.size() == 1 && volley[0].lateralOffset == 10 && ammo == 0);
    assert(fireWeapon(gatling, state, true, 200000, ammo).empty());

    state = {};
    ammo = 10;
    auto alternating = gatling;
    alternating.alternateSides = true;
    assert(fireWeapon(alternating, state, true, 0, ammo)[0].lateralOffset == 10);
    assert(fireWeapon(alternating, state, true, 98, ammo)[0].lateralOffset == -10);
    assert(fireWeapon(alternating, state, true, 196, ammo)[0].lateralOffset == 10);
    assert(ammo == 7);

    // Release and dry attempts must not restart the original per-weapon clock
    // or consume a side in the alternating sequence (444ca0 / 443bd0).
    const auto priorFire = state.lastFireMs;
    const auto priorSide = state.alternateLeft;
    assert(fireWeapon(alternating, state, false, 240, ammo).empty());
    assert(fireWeapon(alternating, state, true, 293, ammo).empty());
    assert(state.lastFireMs == priorFire && state.alternateLeft == priorSide);
    ammo = 0;
    assert(fireWeapon(alternating, state, true, 400, ammo).empty());
    assert(state.lastFireMs == priorFire && state.alternateLeft == priorSide);
    ammo = 1; // refilling an expired cooldown can fire immediately
    volley = fireWeapon(alternating, state, true, 400, ammo);
    assert(volley.size() == 1 && volley[0].lateralOffset == -10 && ammo == 0);
    ammo = 1; // refilling during cooldown cannot bypass it
    assert(fireWeapon(alternating, state, true, 497, ammo).empty() && ammo == 1);
    volley = fireWeapon(alternating, state, true, 498, ammo);
    assert(volley.size() == 1 && volley[0].lateralOffset == 10 && ammo == 0);

    // First firing after an idle interval is immediate. Time spent released
    // does not accumulate volleys; the next volley needs a fresh full delay.
    state = {};
    ammo = 20;
    assert(fireWeapon(gatling, state, false, 5000, ammo).empty() && !state.lastFireMs);
    assert(fireWeapon(gatling, state, true, 6000, ammo).size() == 2 && ammo == 18);
    assert(fireWeapon(gatling, state, false, 6050, ammo).empty());
    assert(fireWeapon(gatling, state, true, 6097, ammo).empty() && ammo == 18);
    assert(fireWeapon(gatling, state, true, 6098, ammo).size() == 2 && ammo == 16);

    state = {};
    ammo = 7;
    const WeaponConfig single{150};
    // Original One-by-one bypasses delay after all counted Ammo has been
    //released; it is not a cap of one live projectile. Nonzero counts still
    //allow firing when the normal delay expires, including high-bit words.
    auto oneByOne=single;oneByOne.oneByOne=true;
    for(uint32_t active:{0u,1u,2u,0x80000000u,0xffffffffu}) {
        WeaponState counted;counted.lastFireMs=100;
        uint32_t stock=5;
        const auto early=fireWeapon(oneByOne,counted,true,101,stock,{},active);
        assert(early.size()==(active==0?1:0));
        assert(stock==(active==0?4:5));
        counted.lastFireMs=100;
        assert(fireWeapon(oneByOne,counted,true,250,stock,{},active).size()==1);
    }
    { WeaponState ordinary;ordinary.lastFireMs=100;uint32_t stock=5;
      assert(fireWeapon(single,ordinary,true,101,stock,{},0).empty());
      assert(fireWeapon(oneByOne,ordinary,false,101,stock,{},0).empty());
      stock=0;assert(fireWeapon(oneByOne,ordinary,true,101,stock,{},0).empty()); }
    volley = fireWeapon(single, state, true, 20, ammo);
    assert(volley.size() == 1 && volley[0].lateralOffset == 0 && ammo == 6);
    state = {};
    auto rightOnly = gatling;
    rightOnly.leftSide = false;
    volley = fireWeapon(rightOnly, state, true, 0, ammo);
    assert(volley.size() == 1 && volley[0].lateralOffset == -10 && ammo == 5);
    state = {};
    auto disabled = rightOnly;
    disabled.rightSide = false;
    assert(fireWeapon(disabled, state, true, 0, ammo).empty() && ammo == 5);
    for (const float gap : {-1.f, std::numeric_limits<float>::infinity(),
                           std::numeric_limits<float>::quiet_NaN()}) {
        state = {};
        auto invalid = gatling;
        invalid.weaponsGap = gap;
        assert(fireWeapon(invalid, state, true, 0, ammo).empty() && !state.lastFireMs);
    }
    state = {};
    const auto end = std::numeric_limits<uint64_t>::max();
    assert(fireWeapon(single, state, true, end - 150, ammo).size() == 1);
    assert(fireWeapon(single, state, true, end, ammo).size() == 1);

    ProjectileMotionConfig motion;motion.speedMax=300;
    ProjectileMotionState bullet;bullet.thrust=300;
    auto delta=advanceProjectileMotion(motion,bullet,1,0,14);
    assert(std::abs(delta.x-29.1f)<.0001f && delta.z==0);
    delta=advanceProjectileMotion(motion,bullet,1,0,28);
    assert(std::abs(delta.x-58.2f)<.0001f); // time normalization, no inertia
    motion.speedMax=60;motion.acceleration=1;bullet={};bullet.thrust=45;
    delta=advanceProjectileMotion(motion,bullet,1,0,14);
    assert(bullet.thrust==46 && std::abs(delta.x-4.462f)<.0001f);
    for(int i=0;i<30;++i) advanceProjectileMotion(motion,bullet,1,0,14);
    assert(bullet.thrust==60);
    motion.inertia=true;motion.launched=true;motion.deceleration=1;
    bullet={9,0,9,90};
    delta=advanceProjectileMotion(motion,bullet,0,1,14);
    assert(bullet.thrust==89 && delta.z==0 && std::abs(delta.x-8.73f)<.0001f);
    motion.launched=false;motion.grip=.1f;motion.acceleration=100;motion.speedMax=65;
    bullet={6.5f,0,6.5f,65};
    delta=advanceProjectileMotion(motion,bullet,0,1,14);
    assert(delta.z==0 && std::abs(delta.x-.6305f)<.0001f); // cap compares prior magnitude
    delta=advanceProjectileMotion(motion,bullet,0,1,14);
    assert(delta.z>6); // next tick permits thrust in new direction
    // MissileVZ Thrust.Max100 differs from its Speed-max65 gate.
    bullet={0,0,0,65};
    delta=advanceProjectileMotion(motion,bullet,0,1,14,100);
    assert(bullet.thrust==100 && std::abs(bullet.magnitude-9.7f)<.00001f);
    delta=advanceProjectileMotion(motion,bullet,0,1,14,100);
    assert(std::abs(bullet.magnitude-.9409f)<.00001f);
    delta=advanceProjectileMotion(motion,bullet,0,1,14,100);
    assert(bullet.magnitude>9.79f);
    const auto before=bullet;
    assert(advanceProjectileMotion(motion,bullet,0,0,14).x==0);
    motion.weight=0;
    assert(advanceProjectileMotion(motion,bullet,1,0,14).x==0 && bullet.thrust==before.thrust);
    motion.weight=1;
    assert(advanceProjectileMotion(motion,bullet,1,0,std::numeric_limits<float>::quiet_NaN()).x==0);
    assert(advanceProjectileMotion(motion,bullet,1,0,101).x==0 && bullet.velocityX==before.velocityX);
    assert(projectileBounceAltitude(9,0)==0);
    assert(projectileBounceAltitude(9,157.07963f)>=89);
    assert(projectileBounceAltitude(9,471.2389f)>=89);
    assert(projectileBounceAltitude(9,-1)==0);
    assert(projectileBounceSignedAltitude(9,471.2389f)<=-89);
    assert(projectileBounceSignedAltitude(.01f,315)==0); // truncates before water decision
    assert(projectileBounceAltitude(9,157.07963f,100)>=89);
}
