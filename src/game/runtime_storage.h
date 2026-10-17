#pragma once
#include <cstddef>
#include <new>
#include <vector>

namespace vyruz {
// Existing World factory limit. Capacity does not construct bodies, consume
// native UIDs, populate cells or change the number of actors saved/visited.
inline constexpr std::size_t RuntimeActorLimit=100000;

template<class T>
void reserveRuntimeStorage(std::vector<T>& values,std::size_t capacity=RuntimeActorLimit) {
    // Reserve during loading/restore so a birth does not move tens of MB of
    // actor state in a gameplay frame. Unused capacity has no initialized rows.
    // Optional headroom must not make an otherwise viable low-memory load fail.
    try { values.reserve(capacity); }
    catch(const std::bad_alloc&) { /* Retain normal vector growth as fallback. */ }
}
}
