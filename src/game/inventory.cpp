#include "game/inventory.h"
#include <algorithm>
#include <map>
namespace vyruz {
std::vector<InventoryRow> inventoryRows(const ActorState& p,int language) {
    std::vector<InventoryRow> out;
    std::vector<int> codes,keys;
    //444796 prepends: newest held item is encountered first by43a120.
    for(auto it=p.heldKeys.rbegin();it!=p.heldKeys.rend();++it)if(it->code &&
        std::find(codes.begin(),codes.end(),it->id)==codes.end())codes.push_back(it->id);
    for(int id:codes) {
        const KeyPiece* first=nullptr;
        for(auto it=p.heldKeys.rbegin();it!=p.heldKeys.rend();++it)if(it->code && it->id==id){first=&*it;break;}
        std::string label="Code #"+std::to_string(id)+" : ";
        const int total=std::clamp(first->total,1,64);
        for(int part=1;part<=total;++part) {
            if(part>1)label+="-";
            // Last matching native-list entry wins: oldest chronological piece.
            const KeyPiece* found=nullptr;
            for(auto it=p.heldKeys.rbegin();it!=p.heldKeys.rend();++it)
                if(it->code && it->id==id && it->part==part)found=&*it;
            label+=found?found->text:"???";
        }
        out.push_back({std::move(label),-1,false,first->held?&*first->held:nullptr});
    }
    if(!out.empty())out.push_back({{},-1,true});
    for(auto it=p.heldKeys.rbegin();it!=p.heldKeys.rend();++it)if(!it->code) {
        keys.push_back(it->id);
        out.push_back({std::string(language==2?"Clef #":"Key #")+std::to_string(it->id),-1,false,it->held?&*it->held:nullptr});
    }
    // Legacy entries have no recoverable artwork; retain their readable label.
    for(int id:p.keys)if(std::find(keys.begin(),keys.end(),id)==keys.end()) {
        keys.push_back(id);out.push_back({std::string(language==2?"Clef #":"Key #")+std::to_string(id)});
    }
    if(!keys.empty())out.push_back({{},-1,true});
    for(size_t i=0;i<p.weapons.size();++i) {
        const auto& w=p.weapons[i];const auto ammo=weaponStock(p,w);
        if(ammo==p.ammo.end() || !ammo->second.count)continue;
        out.push_back({w.name+" ("+std::to_string(ammo->second.count)+"/"+std::to_string(ammo->second.maximum)+")",static_cast<int>(i)});
    }
    if(out.empty())out.push_back({"INVENTORY EMPTY"});
    return out;
}
void InventoryView::scroll(int delta,size_t count,size_t visible) {
    const auto maximum=count>visible?count-visible:0;
    const auto next=static_cast<int64_t>(firstRow)+delta;
    firstRow=static_cast<size_t>(std::clamp<int64_t>(next,0,maximum));
}
}
