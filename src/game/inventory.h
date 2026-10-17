#pragma once
#include "game/world.h"
namespace vyruz {
struct InventoryRow { std::string label;int weaponSlot=-1;bool separator=false; const HeldCode* icon=nullptr; };
std::vector<InventoryRow> inventoryRows(const ActorState& player,int language=1);
// Modal state returns to the prior pause state; it never selects a weapon.
struct InventoryView {
    bool open=false,wasPaused=false;
    size_t firstRow=0;
    void toggle(bool& paused) {if(open)close(paused);else{open=true;wasPaused=paused;paused=true;firstRow=0;}}
    void close(bool& paused) {if(open){open=false;paused=wasPaused;}}
    void scroll(int delta,size_t count,size_t visible);
};
}
