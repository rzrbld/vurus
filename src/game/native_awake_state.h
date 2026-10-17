#pragma once
#include "game/native_awake_manager.h"
#include <map>

namespace vyruz {
// Stable port references preserve native mixed ordering without heavyweight
// actors for bare Ammo. Physical pool identities remain a separate contract.
constexpr std::uint64_t NativeAwakeAmmoBit=std::uint64_t(1)<<63;
struct NativeAwakeState {
    NativeAwakeManager manager;
    std::vector<std::optional<std::size_t>> actors;
    std::map<std::uint64_t,std::size_t> ammo;
};
}
