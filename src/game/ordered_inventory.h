#pragma once
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>
#include <utility>
#include <stdexcept>
namespace vyruz {
// Native ADD records retain authored list order, including repeated names.
// Name access addresses the first record; gameplay selection can address a slot.
struct AmmoStock { uint32_t count=0, maximum=0; };
template<class T> struct OrderedInventory {
    using value_type=std::pair<std::string,T>;
    std::vector<value_type> slots;
    auto begin(){return slots.begin();} auto end(){return slots.end();}
    auto begin()const{return slots.begin();} auto end()const{return slots.end();}
    auto find(const std::string& name){return std::find_if(begin(),end(),[&](const auto& v){return v.first==name;});}
    auto find(const std::string& name)const{return std::find_if(begin(),end(),[&](const auto& v){return v.first==name;});}
    bool contains(const std::string& name)const{return find(name)!=end();}
    bool empty()const{return slots.empty();} size_t size()const{return slots.size();}
    void clear(){slots.clear();}
    void append(std::string name,T value){slots.emplace_back(std::move(name),std::move(value));}
    T& operator[](const std::string& name){auto it=find(name);if(it==end()){append(name,{});return slots.back().second;}return it->second;}
    T& at(const std::string& name){auto it=find(name);if(it==end())throw std::out_of_range("ammo stock");return it->second;}
    const T& at(const std::string& name)const{auto it=find(name);if(it==end())throw std::out_of_range("ammo stock");return it->second;}
};
// 41ca90 (branch 41ccaa) consumes every saved namesake for the first live matching record.
inline void restoreAmmoBaseline(OrderedInventory<AmmoStock>& live,OrderedInventory<AmmoStock> saved) {
    for(auto& [name,stock]:live) {
        for(auto it=saved.begin();it!=saved.end();) {
            if(it->first==name){stock.count=it->second.count;it=saved.slots.erase(it);}
            else ++it;
        }
    }
}
}
