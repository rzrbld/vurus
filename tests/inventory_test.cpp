#include "game/inventory.h"
#include <cstdlib>
#include <iostream>
using namespace vyruz;
void check(bool b){if(!b){std::cerr<<"inventory regression\n";std::exit(1);}}
int main(){
    ActorState p;p.heldKeys={{3,true,2,2,"WORLD"},{3,true,1,2,"HELLO"},{8,false,1,1,{}}};
    EquippedWeapon w;w.name="Gatling";w.ammo="bullet";p.weapons.push_back(w);p.ammo["bullet"]={20,100};
    auto rows=inventoryRows(p);check(rows.size()==5&&rows[0].label=="Code #3 : HELLO-WORLD");
    check(rows[2].label=="Key #8"&&rows[4].label=="Gatling (20/100)"&&rows[4].weaponSlot==0);
    p.ammo["bullet"].count=0;check(inventoryRows(p).size()==4);
    bool paused=false;InventoryView v;v.toggle(paused);check(v.open&&paused);
    v.scroll(99,10,7);check(v.firstRow==3);v.scroll(-99,10,7);check(v.firstRow==0);
    v.close(paused);check(!v.open&&!paused);
    paused=true;v.toggle(paused);v.toggle(paused);check(paused);
    p.heldKeys={{9,true,1,2,"OLD"},{3,true,1,2,""},{9,true,1,2,"NEW"},{7,false,1,1,{}}};
    rows=inventoryRows(p,2);
    check(rows[0].label=="Code #9 : OLD-???" && rows[1].label=="Code #3 : -???");
    check(rows[3].label=="Clef #7");
    p.heldKeys.clear();p.keys.clear();check(inventoryRows(p)[0].label=="INVENTORY EMPTY");
}
