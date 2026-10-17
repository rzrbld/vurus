#include "game/native_navigation.h"
#include <algorithm>
#include <array>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace vyruz;
static void need(bool b,const char*why){if(!b)throw std::runtime_error(why);}
struct Record{uint32_t layout,reverse,onlyPlayer,enrolled,bbox[8],flags[8],edges[64];};
int main(int argc,char**argv){try{need(argc==1||argc==2,"usage: native_initial_graph_native_test [original-records]");std::ifstream in;if(argc==2){in.open(argv[1],std::ios::binary);need(bool(in),"missing original initial graph records");}uint64_t hash=14695981039346656037ull;unsigned count=0;
 for(unsigned layout=0;layout<4;++layout)for(unsigned reverse=0;reverse<2;++reverse)for(unsigned only=0;only<2;++only){
  Record actual{};actual.layout=layout;actual.reverse=reverse;actual.onlyPlayer=only;actual.enrolled=183;NativeNavGraph graph;
  struct Body{unsigned id,x,y;};std::vector<Body> order;for(unsigned id=0;id<8;++id){actual.bbox[id]=id==1||id==2||id==6?1:0;
   if(id==3||id==6)continue;actual.flags[id]=(id==4?12u:20u)|(id==5&&only?2u:0u);
   order.push_back({id,layout==0?0u:layout==2?1u:id&1,layout==0?0u:layout==2?1u:(id>>(layout==3?2:1))&1});}
  //40f090 visits Y descending, then X descending, then the supplied cell's
  //linked head order. These fixture cells were populated with prepend calls.
  std::sort(order.begin(),order.end(),[&](auto a,auto b){if(a.y!=b.y)return a.y>b.y;if(a.x!=b.x)return a.x>b.x;return reverse?a.id<b.id:a.id>b.id;});
  for(auto body:order){float c=layout==2?16:8+body.id*8;insertNativeNavigationObject(graph,body.id,{c,c,body.id==7?c:c+8,body.id==7?c:c+8});}
  need(validNativeNavigationGraph(graph),"initial graph invariant");
  std::fill(std::begin(actual.edges),std::end(actual.edges),UINT32_MAX);
  for(unsigned axis=0;axis<2;++axis){const auto& edges=axis?graph.yEdges:graph.xEdges;for(unsigned n=0;n<edges.size();++n){auto e=edges[n];actual.edges[axis*32+n]=graph.objects[e.object].reference*4+axis+(e.maximum?2:0);}}
  if(argc==2){Record original{};need(bool(in.read(reinterpret_cast<char*>(&original),sizeof original)),"short original graph records");const auto*a=reinterpret_cast<const uint32_t*>(&actual);const auto*b=reinterpret_cast<const uint32_t*>(&original);for(unsigned n=0;n<84;++n)need(a[n]==b[n],"original graph enrollment/flags/axis order mismatch");}
  const auto* bytes=reinterpret_cast<const unsigned char*>(&actual);for(unsigned n=0;n<sizeof actual;++n){hash^=bytes[n];hash*=1099511628211ull;}++count;
 }
 need(hash==0x44fd8739540bdf25ull,"original initial graph fingerprint");if(argc==2){char extra;need(!in.read(&extra,1),"extra original graph records");}
 std::cout<<"PASS "<<count<<" whole original40f090/40ee40/40e210/40e360 graph enrollment and axis histories\n";return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
