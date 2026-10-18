#include "render/sprite_filter.h"
#include "sprite_filter_reference.h"
#include <iostream>
#include <stdexcept>
static void need(bool b,const char* e){if(!b)throw std::runtime_error(e);}
int main(){try{
 uint32_t rng=234567;size_t comparisons=0;
 for(int h:{1,2,3,7,16,31,64,127})for(int w:{1,2,3,5,16,33,64,129})for(int pattern=0;pattern<5;++pattern){
  std::vector<uint8_t> pixels(size_t(w)*h*4);
  for(size_t i=0;i<pixels.size();i+=4){for(int c=0;c<3;++c){rng=rng*1664525u+1013904223u;pixels[i+c]=rng>>24;}
   pixels[i+3]=pattern==0?0:pattern==1?255:pattern==2?(i==0?255:0):pattern==3?(rng&1?255:0):uint8_t(rng%4?0:rng>>24);}
  const auto expected=sprite_filter_reference::filteredSpriteMips(pixels.data(),w,h);
  for(bool mips:{false,true}){size_t emitted=0;
   vyruz::visitFilteredSpriteMips({w,h,pixels},mips,[&](size_t level,const vyruz::SpriteMip& mip){
    need(level==emitted++ && level<expected.size(),"level ordering");need(mip.width==expected[level].width&&mip.height==expected[level].height&&mip.rgba==expected[level].rgba,"mip pixel mismatch");++comparisons;});
   need(emitted==(mips?expected.size():1),"Bilinear emitted lower levels");}
 }
 // Equal-distance tie remains the first row-major opaque seed (red).
 std::vector<uint8_t> tie{255,0,0,255,255,0,255,0,0,0,255,255};
 vyruz::dilateSpriteEdges(tie,3,1);need(tie[4]==255&&tie[5]==0&&tie[6]==0&&tie[7]==0,"dilation tie/alpha changed");
 std::cout<<"PASS "<<comparisons<<" mip comparisons against frozen reference\n";
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
