#include "game/native_projection_cache.h"
#include "game/spider_contact_native.h"
#include <bit>
#include <cmath>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
using namespace vyruz;
static void need(bool b,const char* why){if(!b)throw std::runtime_error(why);}
static bool same(Displacement a,Displacement b){return std::bit_cast<uint32_t>(a.x)==std::bit_cast<uint32_t>(b.x)&&std::bit_cast<uint32_t>(a.y)==std::bit_cast<uint32_t>(b.y);}
int main(){try{
 NativeProjectionCache cache;unsigned calls=0;
 auto project=[&](float x,float y){++calls;return spiderSurfaceToNative(x,y);};
 auto check=[&](float x,float y){need(same(cache.resolve(x,y,project),spiderSurfaceToNative(x,y)),"cached projection retains every output bit");};
 check(0,0);check(0,0);need(calls==1,"repeated position avoids projection");
 check(-0.f,0);need(calls==2,"signed X zero invalidates");check(-0.f,-0.f);need(calls==3,"signed Y zero invalidates");
 float x=65536.f,y=32768.f;check(x,y);auto before=calls;
 x=std::nextafter(x,std::numeric_limits<float>::infinity());check(x,y);need(calls==before+1,"single X ULP invalidates without cell change");
 before=calls;y=std::nextafter(y,0.f);check(x,y);need(calls==before+1,"single Y ULP invalidates without cell change");
 std::mt19937 random(8091);
 for(unsigned i=0;i<10000;++i){x=float(int(random()%200000)-100000)*.03125f;y=float(int(random()%200000)-100000)*.015625f;check(x,y);before=calls;for(int j=0;j<4;++j)check(x,y);need(calls==before,"stationary actor repeatedly hits cache");}
 before=calls;cache.resolve(std::numeric_limits<float>::infinity(),y,project);cache.resolve(std::numeric_limits<float>::infinity(),y,project);cache.resolve(std::numeric_limits<float>::quiet_NaN(),y,project);need(calls==before+3,"nonfinite input never populates cache");
 auto copy=cache;need(same(copy.resolve(x,y,project),spiderSurfaceToNative(x,y)),"copied derived entry retains exact projection");need(calls==before+3,"invalid input does not poison prior finite entry");
 std::cout<<"PASS 10000 exact coordinate cases, repeated hits, signed-zero/ULP invalidation, nonfinite bypass; entry bytes="<<sizeof(cache)<<'\n';
 return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
