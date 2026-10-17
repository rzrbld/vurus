#include "render/crt_geometry.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <iostream>
int main(){
 for(auto size:{vyruz::CrtPoint{640,480},{1280,800},{1080,1080},{2560,720},{5120,1440},{640,1280}}){
  vyruz::CrtGeometry g(size.x,size.y);assert(std::isfinite(g.stretchScale)&&g.stretchScale>0);
  for(int y=0;y<=40;++y)for(int x=0;x<=40;++x){auto p=g.source(x/40.f,y/40.f);assert(std::isfinite(p.x)&&std::isfinite(p.y));if(x<40)assert(g.source((x+1)/40.f,y/40.f).x>p.x);if(y<40)assert(g.source(x/40.f,(y+1)/40.f).y>p.y);
   auto xy=g.forward({(p.x-.5f)*g.aspect.x,(p.y-.5f)*g.aspect.y});const float u=(xy.x-g.stretchX)/g.aspect.x/g.stretchScale+.5f,v=(xy.y-g.stretchY)/g.aspect.y/g.stretchScale+.5f;
   assert(std::abs(u-x/40.f)<.003f&&std::abs(v-y/40.f)<.003f);
  }
 }
 std::cout<<"PASS finite monotone CRT coordinates and inverse pointer mapping at six aspects\n";
}
