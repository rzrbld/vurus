#include "game/interactions.h"
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

namespace {
void check(bool ok, const char* message) {
    if (!ok) { std::cerr << message << '\n'; std::exit(1); }
}
bool near(float a, float b) { return std::abs(a - b) < 0.0001f; }
}
int main() {
    std::vector<vyruz::KeyPiece> keys{{7,true,2,2}};
    check(!vyruz::hasKey(keys,7),"partial code must not unlock");
    keys.push_back({7,true,1,2});
    check(vyruz::hasKey(keys,7),"complete authored code");
    keys.push_back({7,true,1,2});
    check(!vyruz::hasKey(keys,7),"native exact code count rejects excess objects");
    vyruz::consumeKey(keys,7);check(keys.empty(),"consume removes all matching parts");
    vyruz::VitalStats s{5000, 0, 10000, 5000, 0, 10000};
    check(!vyruz::applyDamage(s, 5000) && s.shield == 0 && s.energy == 5000,
          "exact shield depletion leaked damage");
    check(!vyruz::applyDamage(s, 100) && s.energy == 4900, "unshielded damage lost");
    s.shield = 100; s.shieldMin = 5;
    check(!vyruz::applyDamage(s, 150) && s.shield == 5 && s.energy == 4850,
          "shield overflow/minimum differs");
    check(!vyruz::applyDamage(s, 100000, true) && s.energy == 4850, "indestructible damaged");
    check(vyruz::applyDamage(s, 6000) && s.energy < 0, "death transition lost");
    check(!vyruz::applyDamage(s, 1), "death reported twice");
    s = {9499, 0, 10000, 9999, 0, 10000};
    check(vyruz::applyPickup(s, {1000, 1000, 500}) && s.energy == 10000 &&
          s.shield == 10000 && s.money == 500 && s.lifetimeMoney == 500, "pickup not capped");
    check(!vyruz::applyPickup(s, {1000, 1000, 0}), "full resource should remain");
    s.energy = 9500;
    check(!vyruz::applyPickup(s, {500, 0, 0}) && s.energy == 9500,
          "95 percent boundary consumed repair");
    s.shield = 9999;
    check(vyruz::applyPickup(s, {500, 1, 0}) && s.energy == 9500 && s.shield == 10000,
          "combined pickup did not preserve independent eligibility");
    check(!vyruz::applyPickup(s, {-1, -1, -1}), "negative unsupported pickup accepted");
    const float old = s.energy;
    check(!vyruz::applyDamage(s, std::numeric_limits<float>::infinity()) && s.energy == old,
          "invalid damage corrupted stats");
    s.money = std::numeric_limits<int64_t>::max() - 1;
    vyruz::applyPickup(s, {0, 0, 500});
    check(s.money == std::numeric_limits<int64_t>::max(), "money overflowed");

    // Radius10 spans sqrt(160)=12.649 horizontal pixels, sqrt(40)=6.325 vertical.
    const float rx = std::sqrt(160.f), ry = std::sqrt(40.f);
    auto t = vyruz::sweptCircleContact(-30, 0, 30, 0, 0, 0, 10);
    check(t && near(*t, (30-rx)/60), "horizontal sweep missed or tunneled");
    t = vyruz::sweptCircleContact(0, -30, 0, 30, 0, 0, 10);
    check(t && near(*t, (30-ry)/60), "vertical metric wrong");
    check(!vyruz::sweptCircleContact(-30, 20, 30, 20, 0, 0, 10), "separated sweep hit");
    check(!vyruz::sweptCircleContact(5, 0, 20, 0, 0, 0, 10), "overlap cannot escape");
    check(!vyruz::sweptCircleContact(5, 0, 5, 1, 0, 0, 10), "overlap tangent blocked");
    t = vyruz::sweptCircleContact(5, 0, 0, 0, 0, 0, 10);
    check(t && *t == 0, "deeper penetration allowed");
    check(!vyruz::sweptCircleContact(0, 0, 0, 0, 0, 0, 10), "stationary sweep reported hit");
    std::cout << "interaction tests passed\n";
}
