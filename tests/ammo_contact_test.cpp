#include "game/ammo_contact.h"
#include <cassert>
#include <cmath>
int main(){
 using namespace vyruz;
 const auto hit=[](NativeContactPoint from,NativeContactPoint to){return nativeAmmoCircleContact(from,to,22,{0,0},20);};
 assert(!hit({0,0},{0,0})); // movement-driven, never stationary proximity
 assert(hit({-80,0},{-41.5,0}));
 assert(!hit({-80,0},{-41.50001,0})); // asymmetric 410a00 half-unit bounds
 assert(hit({80,0},{42.5,0}));
 assert(!hit({80,0},{42.50001,0}));
 assert(hit({0,-80},{0,-41.5}));
 assert(hit({0,80},{0,42.5}));
 assert(!hit({-80,0},{80,0})); // broadphase sweep alone is not native narrowphase
 const auto diagonal=hit({-50,-50},{-30,-30});assert(diagonal);
 const double d=std::sqrt(1800.)-2,scale=(d-20)/d;
 assert(std::abs(diagonal->x-(-30+30*scale))<1e-10);
 assert(!hit({-50,-50},{-32,-32})); // circle test excludes overlapping AABBs
 assert(hit({0,0},{.1,0})); // starting inside then moving can trigger
}
