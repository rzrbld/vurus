#include "game/pickup_candidate_index.h"
#include <cstdlib>
#include <iostream>
#include <numeric>
#include <random>
#include <vector>
using namespace vyruz;
void need(bool b,const char* s){if(!b){std::cerr<<s<<'\n';std::exit(1);}}
std::vector<size_t> replay(unsigned seed,bool indexed){
 std::mt19937 rng(seed);std::vector<bool> roles(40);PickupCandidateIndex index;
 for(size_t i=0;i<roles.size();++i){roles[i]=rng()%3==0;if(roles[i])index.include(i);}
 struct Birth{size_t slot;bool pickup;};std::vector<std::vector<Birth>> births(100);
 for(auto& row:births){for(unsigned j=0;j<rng()%5;++j)row.push_back({rng()%80,rng()%2!=0});}
 PickupCandidateIndex::Cursor cursor;size_t plain=0;std::vector<size_t> trace;
 for(;;){std::optional<size_t> id;if(indexed)id=index.next(cursor);else if(plain<roles.size())id=plain++;
  if(!id)break;if(*id>=roles.size()||!roles[*id])continue;trace.push_back(*id);
  for(auto b:births[*id]){if(b.slot>=roles.size())roles.resize(b.slot+1);roles[b.slot]=b.pickup;if(b.pickup)index.include(b.slot);}
 }
 return trace;
}
int main(){
 PickupCandidateIndex x;for(auto i:{10u,20u,30u})x.include(i);
 PickupCandidateIndex::Cursor c;need(x.next(c)==10,"first numeric slot");
 x.include(2);x.include(5);x.include(15);x.include(20);x.include(40);
 need(x.next(c)==15,"skip below-cursor births; visit intervening birth");
 need(x.next(c)==20&&x.next(c)==30&&x.next(c)==40&&!x.next(c),"ascending order and duplicate suppression");
 x.include(50);need(x.next(c)==50,"later high birth remains visible");
 x.clear();PickupCandidateIndex::Cursor fresh;need(!x.next(fresh),"clear for load/accepted restore");
 for(unsigned seed=0;seed<10000;++seed)need(replay(seed,false)==replay(seed,true),"dynamic slot publication matches full numeric traversal");
 std::cout<<"10000 dynamic publication comparisons and cursor boundary checks passed\n";
}
