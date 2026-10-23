// Original-instruction point-source graph and geometry fixtures; no game data required.
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
static void pointEdges(){
constexpr char hit[]={
"0000000000000000111111111100111100110000000000001100111100111111"
"1100000000000000000000000000000000001111111100110000110000000000"
"0000001100001111111111000000000000000000000000000000000011111111"
"1111110000000000000000000000001100111111110011000000000000000000"
"0000000000111111111111001100000000000000000000000000000011111100"
"0000000000000000000000000000000011111111110011110011000000000000"
"1100111100111111110000000000000000000000000000000000111111110011"
"0000110000000000000000110000111111111100000000000000000000000000"
"0000000011111111111111000000000000000000000000110011111111001100"
"0000000000000000000000000011111111111100110000000000000000000000"
"00000000111111000000000000000000"
};
unsigned row=0;const float coordinates[]={-40,-20,-5,0,5,20,40};
for(unsigned shift=0;shift<2;++shift)for(unsigned movement=0;movement<8;++movement)for(unsigned layout=0;layout<7;++layout)for(unsigned shape=0;shape<3;++shape)for(unsigned reverse=0;reverse<2;++reverse){
 ContactVelocity from{shift?1000.13f:0,shift?1000.27f:0};const ContactVelocity to{from.x+motion[movement][0],from.z+motion[movement][1]};
 const float c=coordinates[layout];ContactVelocity a{from.x+(shape==0?c:-15),from.z+(shape==1?c:-15)},b{from.x+(shape==0?c:15),from.z+(shape==1?c:15)};
 std::optional<ContactVelocity> point;
 if(shape==2){a={from.x+c,from.z-5};b={from.x+c+10,from.z+5};const ContactVelocity v[]={a,{b.x,a.z},b,{a.x,b.z}};for(unsigned n=0;n<4&&!point;++n)point=nativeAmmoPointEdge(from,to,v[n],v[(n+1)%4]);}
 else {if(reverse)std::swap(a,b);point=nativeAmmoPointEdge(from,to,a,b);}
 need(bool(point)==bool(hit[row++]-'0'),"native point/edge/rectangle result");
}
need(row==672,"complete native edge matrix");
const auto circle=nativeAmmoPointCircle({0,0},{20,0},{23,0},8,true);
need(circle&&circle->x==15&&circle->z==0,"native point enters circle at15");
}

int main(int argc,char**argv){try{
 constexpr char expected[]={
"2222222222222222000000002122112111221111112211112222222211111111"
"0000000000000000112211111122111122221122000000002222112222222222"
"2222222211221111122211121122111122222222000000001122111100000000"
"1222111211221111111211110000000022222222222222220000000011221111"
"1122111111221111222222220000000000000000000000001122111111221111"
"1122111100000000222222222222112222222222112211111122111111221111"
"2222222200000000000000000000000011221111112211111122111100000000"
"2222222222222222000000001122111111221111112211112222222200000000"
"0000000022222222112211111122111122221122000000002222222222222222"
"0000000011221111112211111122111122222222000000000000000000000000"
"1122111111221111112211110000000022222222222222220000000011221111"
"1122111111221111222222220000000000000000000000001122111111221111"
"2222112200000000000000000000000000000000000000000000000000000000"
"0000000000000000000000000000000000000000000000000000000000000000"
};
 std::ifstream in;if(argc==2){in.open(argv[1],std::ios::binary);need(bool(in),"native graph records missing");}
 unsigned total=0,failed=0,finite=0,singular=0;float maximum=0;
 for(uint32_t movement=0;movement<8;++movement)for(uint32_t layout=0;layout<14;++layout)for(uint32_t types=0;types<4;++types)for(uint32_t reverseOrder=0;reverseOrder<2;++reverseOrder){
 Record r{movement,layout,types,reverseOrder,0,uint32_t(expected[total]-'0'),0,0};
 if(argc==2){Record native;need(bool(in.read(reinterpret_cast<char*>(&native),sizeof native)),"short native graph record");need(native.movement==movement&&native.positions==layout&&native.types==types&&native.reverseOrder==reverseOrder,"native graph input/selection mismatch");r=native;}
 NativeNavGraph g;ContactVelocity proposed{motion[r.movement][0],motion[r.movement][1]};std::array<ContactVelocity,3> positions{{{}, {proposed.x+offsets[r.positions][0],proposed.z+offsets[r.positions][1]}, {proposed.x+offsets[r.positions][2],proposed.z+offsets[r.positions][3]}}};
 const float radii[3]={0,8,12};
 for(unsigned a=0;a<3;++a){auto p=positions[a];const float radius=radii[a];g.objects.push_back({a,{p.x-radius,p.z-radius,p.x+radius,p.z+radius}});}
 for(unsigned axis=0;axis<2;++axis){auto& edges=axis?g.yEdges:g.xEdges;for(unsigned a=0;a<3;++a)for(bool max:{false,true})edges.push_back({a,max});
  auto value=[&](const auto& e){auto b=g.objects[e.object].bounds;return axis?(e.maximum?b.maxY:b.minY):(e.maximum?b.maxX:b.minX);};
  std::vector<NativeNavGraphEdge> ordered(edges.begin(),edges.end());std::sort(ordered.begin(),ordered.end(),[&](const auto&a,const auto&b){float x=value(a),y=value(b);unsigned ai=a.object*4+axis+(a.maximum?2:0),bi=b.object*4+axis+(b.maximum?2:0);return x!=y?x<y:r.reverseOrder?ai>bi:ai<bi;});edges.assign(ordered.begin(),ordered.end());
 }
 NativeAmmoContactObject source;source.identity=1;source.subtype=16;source.ageMs=1000;source.shape=2;
 const auto result=queryNativeAmmoShapes(g,0,source,{},proposed,0,[&](uint64_t id)->std::optional<NativeAmmoShapeTarget>{
  if(!id||id>=3)return {};NativeAmmoShapeTarget target;target.filter.identity=id+1;target.filter.ageMs=1000;target.filter.subtype=(r.types&(1<<(id-1)))?18:22;target.center=positions[id];target.radius=radii[id];return target;
 },true,true);need(!result.unsupportedShape,"unexpected noncircle native graph input");
 const unsigned selected=result.contact?result.contact->reference:0;const auto point=result.contact?result.contact->point:ContactVelocity{};
 bool bad=selected!=r.target;
 if(argc==2){need(bool(selected)!=bool(r.result),"native graph return convention");
  if(selected && std::isfinite(r.x)&&std::isfinite(r.z)){float error=std::max(std::abs(point.x-r.x),std::abs(point.z-r.z));maximum=std::max(error,maximum);bad|=error>0.00002f;++finite;}
  else if(selected){need(!std::isfinite(point.x)||!std::isfinite(point.z),"original singular point contact preserved");++singular;}
 }
 if(bad&&failed<16)std::cout<<"MISMATCH "<<r.movement<<','<<r.positions<<','<<r.types<<','<<r.reverseOrder<<" native="<<r.target<<" port="<<selected<<'\n';
 failed+=bad;++total;
}
pointEdges();std::cout<<total<<" point-source original graph comparisons, failures="<<failed<<" finite="<<finite<<" singular="<<singular<<" max="<<maximum<<'\n';return failed!=0;}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
