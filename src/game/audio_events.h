#pragma once
#include "game/content.h"
namespace vyruz {
// Transient presentation events. Never replayed by checkpoint restoration.
enum class AudioEventKind { WeaponFire,WeaponStop,DryFire,Pickup,WeaponSwitch,Spawn,Death,Bonus,Malus,Voice,MapCash };
struct AudioEvent {
    AudioEventKind kind;size_t actor;ContentPosition position;std::string name;
    unsigned delayMs=0,holdMs=1000,tailMs=0;
};
}
