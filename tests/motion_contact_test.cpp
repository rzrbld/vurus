#include "game/motion_contact.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <limits>

using namespace vyruz;
namespace {
bool near(float a, float b) { return std::abs(a-b) < 1e-5f; }
void equal(ContactVelocity a, ContactVelocity b) { assert(near(a.x,b.x) && near(a.z,b.z)); }
}
int main() {
    // Whole-vector elasticity differs from scaling only the reflected normal.
    equal(reflectTerrainVelocity({3,2}, {1,0}, 0.5f), {-1.5f,1});
    equal(reflectTerrainVelocity({3,2}, {1,0}, 0), {0,0});
    equal(reflectTerrainVelocity({-3,2}, {1,0}, 1), {-3,2});
    equal(reflectTerrainVelocity({0,2}, {1,0}, 1), {0,2});
    equal(reflectTerrainVelocity({}, {1,0}, 1), {});
    equal(reflectTerrainVelocity({}, {1,0}, 1, {0.3f,0}), {-0.3f,0});
    equal(reflectTerrainVelocity({}, {1,0}, 1, {-0.3f,0}), {});
    equal(reflectTerrainVelocity({3,2}, {}, 1), {3,2});
    constexpr double pi = 3.14159265358979323846;
    for (int direction=0; direction<24; ++direction) {
        const double angle = direction*pi/12;
        ContactVelocity normal{static_cast<float>(std::cos(angle)), static_cast<float>(std::sin(angle))};
        ContactVelocity velocity{normal.x*2,normal.z*2};
        equal(reflectTerrainVelocity(velocity,normal,1), {-velocity.x,-velocity.z});
        for (int offset=0; offset<24; ++offset) {
            ContactVelocity other{static_cast<float>(std::cos(angle+offset*pi/12)),
                                  static_cast<float>(std::sin(angle+offset*pi/12))};
            assert(contactHeadingAccepted(other,normal) == (std::min(offset,24-offset)<=6));
        }
    }
    const auto stationary = resolveActorContact({3,2}, {}, {1,0}, 15, 30, 1, 1, false);
    assert(stationary.applied); equal(stationary.first, {-3,0}); equal(stationary.second, {});
    const auto equalMass = resolveActorContact({3,2}, {}, {1,0}, 15, 15, 1, 1);
    assert(equalMass.applied); equal(equalMass.first, {}); equal(equalMass.second, {3,0});
    const auto weighted = resolveActorContact({3,2}, {-1,4}, {1,0}, 15, 30, 1, 1);
    assert(weighted.applied); equal(weighted.first, {-7.f/3.f,0}); equal(weighted.second, {5.f/3.f,0});
    // Two separately authored elasticities, not their average/product.
    const auto inelastic = resolveActorContact({4,2}, {}, {1,0}, 15, 15, 0.5f, 0.25f);
    equal(inelastic.first,{1,0}); equal(inelastic.second,{2.5f,0});
    assert(!resolveActorContact({3,2}, {}, {1,0}, 0, 30, 1, 1).applied);
    const float nan=std::numeric_limits<float>::quiet_NaN();
    equal(reflectTerrainVelocity({nan,0},{1,0},1), {});
    equal(reflectTerrainVelocity({1,0},{1,0},nan), {});
    assert(!resolveActorContact({3,2}, {}, {nan,0}, 15,30,1,1).applied);
}
