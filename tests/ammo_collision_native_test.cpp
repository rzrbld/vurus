#include "game/ammo_collision_native.h"
#include <fstream>
#include <iostream>
#include <cstdint>
#include <array>
#include <stdexcept>
using namespace vyruz;
struct Record{uint32_t movement,positions,types,reverseOrder,result,target;float x,z;};
static const float motion[8][2]={{20,0},{-20,0},{0,20},{0,-20},{20,20},{-20,20},{20,-20},{0,0}};
static const float offsets[14][4]={{12,0,-12,0},{0,12,0,-12},{10,10,-10,-10},{3,0,7,0},{7,0,3,0},{0,0,0,0},{40,0,7,0},{-25,0,-35,0},{17,0,18,0},{17,17,-17,-17},{2,0,-2,0},{.5f,0,-.5f,0},{-8,0,8,0},{-50,0,50,0}};
static void need(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
int main(int argc,char**argv){try{
 constexpr char expected[]={
 "1122111122221122222211222222112211221111222211222222222200000000"
 "2222222200000000212211211122111111221111000000002222112222221122"
 "2222112222221122112211112222112222222222000000002222112200000000"
 "2222112211221111112211110000000022221122112211112222112222221122"
 "1122111122221122222222220000000022222222000000002222112211221111"
 "1122111100000000222211222222112222221122222211221122111122221122"
 "2222222200000000222222220000000022221122112211111122111100000000"
 "2222112211221111222211222222112211221111222211222222222200000000"
 "2222222200000000222211221122111111221111000000002222112211221111"
 "2222112222221122112211112222112222222222000000002222112200000000"
 "2222112211221111112211110000000022221122222211222222112222221122"
 "1122111122221122222222220000000022222222000000002222112211221111"
 "1122111100000000000000000000000000000000000000000000000000000000"
 "0000000000000000000000000000000000000000000000000000000000000000"
};
 std::ifstream in;if(argc==2){in.open(argv[1],std::ios::binary);need(bool(in),"native graph records missing");}
 unsigned total=0,failed=0,finite=0,singular=0;float maximum=0;
 for(uint32_t movement=0;movement<8;++movement)for(uint32_t layout=0;layout<14;++layout)for(uint32_t types=0;types<4;++types)for(uint32_t reverseOrder=0;reverseOrder<2;++reverseOrder){
 Record r{movement,layout,types,reverseOrder,0,uint32_t(expected[total]-'0'),0,0};
 if(argc==2){Record native;need(bool(in.read(reinterpret_cast<char*>(&native),sizeof native)),"short native graph record");need(native.movement==movement&&native.positions==layout&&native.types==types&&native.reverseOrder==reverseOrder&&native.target==r.target,"native graph input/selection mismatch");r=native;}
 NativeNavGraph g;ContactVelocity proposed{motion[r.movement][0],motion[r.movement][1]};std::array<ContactVelocity,3> positions{{{}, {proposed.x+offsets[r.positions][0],proposed.z+offsets[r.positions][1]}, {proposed.x+offsets[r.positions][2],proposed.z+offsets[r.positions][3]}}};
 const float radii[3]={8,8,12};
 for(unsigned a=0;a<3;++a){auto p=positions[a];const float radius=radii[a];g.objects.push_back({a,{p.x-radius,p.z-radius,p.x+radius,p.z+radius}});}
 for(unsigned axis=0;axis<2;++axis){auto& edges=axis?g.yEdges:g.xEdges;for(unsigned a=0;a<3;++a)for(bool max:{false,true})edges.push_back({a,max});
  auto value=[&](const auto& e){auto b=g.objects[e.object].bounds;return axis?(e.maximum?b.maxY:b.minY):(e.maximum?b.maxX:b.minX);};
  std::vector<NativeNavGraphEdge> ordered(edges.begin(),edges.end());std::sort(ordered.begin(),ordered.end(),[&](const auto&a,const auto&b){float x=value(a),y=value(b);unsigned ai=a.object*4+axis+(a.maximum?2:0),bi=b.object*4+axis+(b.maximum?2:0);return x!=y?x<y:r.reverseOrder?ai>bi:ai<bi;});edges.assign(ordered.begin(),ordered.end());
 }
 NativeAmmoContactObject source;source.identity=1;source.subtype=16;source.ageMs=1000;
 const auto result=queryNativeAmmoCircles(g,0,source,{},proposed,8,[&](uint64_t id)->std::optional<NativeAmmoCircleTarget>{
  if(!id||id>=3)return {};NativeAmmoCircleTarget target;target.filter.identity=id+1;target.filter.ageMs=1000;target.filter.subtype=(r.types&(1<<(id-1)))?18:22;target.center=positions[id];target.radius=radii[id];return target;
 });need(!result.unsupportedShape,"unexpected noncircle native graph input");
 const unsigned selected=result.contact?result.contact->reference:0;const auto point=result.contact?result.contact->point:ContactVelocity{};
 bool bad=selected!=r.target;
 if(argc==2){need(bool(selected)!=bool(r.result),"native graph return convention");
  if(selected && std::isfinite(r.x)&&std::isfinite(r.z)){float error=std::max(std::abs(point.x-r.x),std::abs(point.z-r.z));maximum=std::max(error,maximum);bad|=error>0.00002f;++finite;}
  else if(selected){need(std::isfinite(point.x)&&std::isfinite(point.z),"port singular contact fallback must remain finite");++singular;}
 }
 if(bad&&failed<16)std::cout<<"MISMATCH "<<r.movement<<','<<r.positions<<','<<r.types<<','<<r.reverseOrder<<" native="<<r.target<<" port="<<selected<<'\n';
 failed+=bad;++total;
}
// Original fractional-history probe includes stationary(.1,.2): rounded
// min/max averages differ from the retained native center, but no axis moves.
 NativeNavGraph fractional;fractional.objects={{0,{.1f-8,.2f-8,.1f+8,.2f+8}},{1,{.1f+8-12,.2f-12,.1f+8+12,.2f+12}}};
 fractional.xEdges={{0,false},{1,false},{0,true},{1,true}};fractional.yEdges={{1,false},{0,false},{0,true},{1,true}};
 NativeAmmoContactObject source;source.identity=1;source.subtype=16;source.ageMs=1000;
 const auto stationary=queryNativeAmmoCircles(fractional,0,source,{.1f,.2f},{.1f,.2f},8,[](uint64_t)->std::optional<NativeAmmoCircleTarget>{NativeAmmoCircleTarget t;t.filter.identity=2;t.center={8.1f,.2f};t.radius=12;return t;});
 need(!stationary.contact,"fractional stationary source must not invent movement from bounds midpoint");
 NativeNavGraph route;route.objects={{0,{-8,-8,8,8}},{0,{1,0,1,0},true,true}};route.xEdges={{0,false},{1,false},{0,true}};route.yEdges={{0,false},{1,false},{0,true}};
 unsigned calls=0;queryNativeAmmoCircles(route,0,source,{0,0},{2,0},8,[&](uint64_t)->std::optional<NativeAmmoCircleTarget>{++calls;return {};});
 need(calls==0,"route point reference0 must not resolve as player actor0");
 std::cout<<total<<" native4105d0 selections, "<<failed<<" failures, "<<finite<<" finite point comparisons, "<<singular<<" deliberate singular fallbacks; max point error "<<maximum<<'\n';return failed||total!=896;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}
}
