#pragma once
#include "game/content.h"
namespace vyruz {
// Values come from the ordered difficulty-level blocks in DIFFICULTY.3DC.
// Missing documents (e.g. synthetic content) retain authored actor values.
struct DifficultyPolicy {
    int level = 1;
    float playerShieldMax = 0, playerEnergyMax = 0;
    float enemyScanDelay = 1, enemyScanRange = 1;
    float enemyShieldRegeneration = 1, playerShieldRegeneration = 1;
    float mineRandomCount = 1, enemyFireDelay = 1;
    float shieldItemValue = 1, energyItemValue = 1;
};
bool loadDifficultyPolicy(const SceneContent&, int level, DifficultyPolicy&, std::string& error);
}
