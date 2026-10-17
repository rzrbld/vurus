#include "game/tile_body_native.h"
#include <cassert>
#include <vector>
int main(){using namespace vyruz;
 NativeTileBodyCatalog c;c.width=c.height=2;c.sources={{},{1,15,1},{1,10,0},{2,3,1},{1,12,1}};
 c.cells={{{1,4,0,2}},{{1,0,0,0}},{{3,0,0,0}},{{0,4,0,0}}};
 std::vector<NativeTileBodyPlacement> got;
 assert(visitNativeTileBodyPlacements(c,[&](auto p){got.push_back(p);return true;}));
 assert(got==(std::vector<NativeTileBodyPlacement>{{0,0,0,1},{0,0,1,4},{1,0,0,1},{1,1,1,4}}));
 unsigned count=0;assert(!visitNativeTileBodyPlacements(c,[&](auto){return ++count<2;}));assert(count==2);
 const std::vector<std::uint8_t> input={64,0,32,0,0,0,0,0,1,0,0,0,0,0,2,0};
 const auto text="# GAF files\n1\nd:/world/data/anim/water/animated.gaf\n# Objects\n2\n1 water\n0 d:/world/data/bitmap/water/ground.tga\n";
 std::string error;assert(decodeNativeTileBodyCatalog(input,text,c,error));
 assert(c.sources[1].kind==1 && c.sources[1].flags==0 && c.sources[2].kind==2);
 assert(c.cells[0][0]==2); // last same-cell, same-slot input wins.
}
