#pragma once
#include <algorithm>
#include <cmath>

namespace vyruz {

struct ContactVelocity { float x = 0.f, z = 0.f; };
inline bool finiteContactVector(ContactVelocity v) { return std::isfinite(v.x) && std::isfinite(v.z); }
inline float contactDot(ContactVelocity a, ContactVelocity b) { return a.x * b.x + a.z * b.z; }
inline ContactVelocity contactUnit(ContactVelocity v) {
    const float length = std::hypot(v.x, v.z);
    return finiteContactVector(v) && std::isfinite(length) && length > 1e-8f ?
        ContactVelocity{v.x / length, v.z / length} : ContactVelocity{};
}

// 436a50 -> 4075c0 / 404330: contact and movement headings must differ
// by at most six of 24 directions. Phase does not matter at the cardinal table
// boundaries; quantization is retained instead of replacing this with a dot test.
inline bool contactHeadingAccepted(ContactVelocity velocity, ContactVelocity inward) {
    if (!finiteContactVector(velocity) || !finiteContactVector(inward)) return false;
    if (contactDot(velocity, velocity) == 0.f || contactDot(inward, inward) == 0.f) return false;
    constexpr double pi = 3.14159265358979323846;
    auto heading = [&](ContactVelocity v) {
        double angle = pi / 2.0 - std::atan2(static_cast<double>(v.z), static_cast<double>(v.x));
        if (angle < 0) angle += 2 * pi;
        return static_cast<int>(std::floor(angle * 12.0 / pi + 0.5)) % 24;
    };
    int delta = std::abs(heading(velocity) - heading(inward));
    return std::min(delta, 24 - delta) <= 6;
}

// Native ordinary terrain branch 4367d0: scale the ENTIRE reflected vector
// by elasticity. A stationary commanded mover uses its thrust*direction table
// vector (436ac0), supplied as stationaryDrive. Degenerate/tangent fallback is
// deliberately conservative: native's arbitrary 3-heading kick is not copied.
inline ContactVelocity reflectTerrainVelocity(ContactVelocity velocity, ContactVelocity inward,
                                              float elasticity, ContactVelocity stationaryDrive = {}) {
    if (!finiteContactVector(velocity) || !std::isfinite(elasticity) || elasticity < 0.f) return {};
    const auto normal = contactUnit(inward);
    auto source = velocity;
    if (contactDot(source, source) == 0.f) source = stationaryDrive;
    if (!contactHeadingAccepted(source, normal)) return velocity;
    const float projection = contactDot(source, normal);
    if (!(projection > 1e-8f)) return velocity; // never bounce a separating or tangent mover inward
    const ContactVelocity result{elasticity * (source.x - 2.f * normal.x * projection),
                                 elasticity * (source.z - 2.f * normal.z * projection)};
    return finiteContactVector(result) ? result : ContactVelocity{};
}

struct ActorContactVelocities { ContactVelocity first, second; bool applied = false; };

// 436dd0: dynamic response replaces both velocities by their weighted normal
// components. The native path does NOT preserve tangential velocity. A target
// with zero friction is immobile and reflects only the first normal projection.
// Both dynamic weights must be positive; invalid authored data safely declines.
inline ActorContactVelocities resolveActorContact(ContactVelocity first, ContactVelocity second,
                                                  ContactVelocity inward, float firstWeight,
                                                  float secondWeight, float firstElasticity,
                                                  float secondElasticity, bool secondMobile = true) {
    ActorContactVelocities result{first, second, false};
    if (!finiteContactVector(first) || !finiteContactVector(second) ||
        !std::isfinite(firstElasticity) || firstElasticity < 0.f ||
        !std::isfinite(secondElasticity) || secondElasticity < 0.f) return result;
    const auto normal = contactUnit(inward);
    if (!contactHeadingAccepted(first, normal)) return result;
    const double u1 = contactDot(first, normal), u2 = contactDot(second, normal);
    if (!(u1 > 1e-8)) return result;
    double v1 = -firstElasticity * u1, v2 = u2;
    if (secondMobile) {
        if (!std::isfinite(firstWeight) || !std::isfinite(secondWeight) ||
            firstWeight <= 0.f || secondWeight <= 0.f) return result;
        const double sum = static_cast<double>(firstWeight) + secondWeight;
        const double shared = (firstWeight * u1 + secondWeight * u2) / sum;
        const double relative = (u1 - u2) / sum;
        v1 = shared - firstElasticity * secondWeight * relative;
        v2 = shared + secondElasticity * firstWeight * relative;
    }
    result.first = {static_cast<float>(normal.x * v1), static_cast<float>(normal.z * v1)};
    if (secondMobile) result.second = {static_cast<float>(normal.x * v2), static_cast<float>(normal.z * v2)};
    if (!finiteContactVector(result.first) || !finiteContactVector(result.second)) return {first, second, false};
    result.applied = true;
    return result;
}

} // namespace vyruz
