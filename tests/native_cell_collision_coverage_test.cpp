#include "game/world.h"
#include <random>
#include <stdexcept>
#include <iostream>
using namespace vyruz;
namespace vyruz {struct CollisionIndexOracleAccess {
 static void run(){
  SceneContent scene;Level level;level.mapW=256;level.mapH=512;GameWorld w;w.scene_=&scene;w.level_=&level;w.player_=0;
  scene.objects.resize(700);w.actors_.resize(700);
  std::mt19937 rng(62523);auto pos=[&](){return float(int(rng()%14000)-2000);};
  for(size_t id=0;id<700;++id){auto& a=w.actors_[id];a.alive=a.solid=true;a.radius=2+rng()%150;scene.objects[id].position={pos(),pos()};}
  w.initializeActorCells();w.prepareCollisionQueries();
  size_t comparisons=0;
  for(unsigned step=0;step<4000;++step){
   const size_t id=1+rng()%699;auto& a=w.actors_[id];
   scene.objects[id].position={pos(),pos()};a.radius=step%101==0?20000:2+rng()%150;
   a.alive=step%13!=0;a.nativeHeldOriginal=step%17==0;w.synchronizeActorCell(id);w.prepareCollisionQueries();
   const ContentPosition from{pos(),pos()},to{from.x+float(int(rng()%200)-100),from.y+float(int(rng()%200)-100)};
   for(bool native:{false,true}){
    w.collisionQueriesActive_=false;const bool expected=w.dynamicBlocked(0,from,to,25,false,native);
    w.collisionQueriesActive_=true;const bool actual=w.dynamicBlocked(0,from,to,25,false,native);
    if(actual!=expected)throw std::runtime_error("live broad-phase vs all-body scan");
    ++comparisons;
   }
  }
  // Birth after index publication must still be tested by the unindexed tail.
  scene.objects.push_back({});scene.objects.back().position={100000,100000};w.actors_.push_back({});
  w.actors_.back().alive=w.actors_.back().solid=true;w.actors_.back().radius=100;
  w.collisionQueriesActive_=false;const bool birthExpected=w.dynamicBlocked(0,{99700,100000},{100000,100000},1,false,false);
  w.collisionQueriesActive_=true;const bool birthActual=w.dynamicBlocked(0,{99700,100000},{100000,100000},1,false,false);
  if(!birthExpected || birthActual!=birthExpected)throw std::runtime_error("newborn tail");
  std::cout<<comparisons<<" live World queries equal independent all-body scan; newborn tail passes\n";
 }
};}
int main(){CollisionIndexOracleAccess::run();}
