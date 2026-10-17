#include "game/native_contact_graph.h"
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace vyruz;
static void need(bool b,const char* message){if(!b)throw std::runtime_error(message);}
struct Record {uint32_t layout,source,hint,movement,reverse,edges[16];};
static constexpr float positions[6][8]={{0,0,8,0,16,0,24,0},{0,0,0,0,0,0,0,0},{0,0,10,10,-10,-10,20,20},{20,30,40,50,60,70,80,90},{-.1f,.2f,8.1f,8.2f,16.1f,16.2f,24.1f,24.2f},{0,0,-8,8,8,-8,0,16}};
static constexpr float movement[12][2]={{0,0},{1,0},{-1,0},{0,1},{0,-1},{8,8},{-8,-8},{32,0},{-32,0},{0,32},{0,-32},{32,-32}};
int main(int argc,char**argv){try{need(argc==1||argc==2,"usage: native_contact_graph_native_test [native-records]");std::ifstream in;if(argc==2){in.open(argv[1],std::ios::binary);need(bool(in),"missing original records");}unsigned count=0,failures=0,changed=0;uint64_t fingerprint=14695981039346656037ull;
 for(unsigned layout=0;layout<6;++layout)for(unsigned source=0;source<4;++source)for(unsigned hint=0;hint<4;++hint)if(source!=hint)for(unsigned m=0;m<12;++m)for(unsigned reverse=0;reverse<2;++reverse){
  Record r{layout,source,hint,m,reverse,{}};if(argc==2){Record original{};need(bool(in.read(reinterpret_cast<char*>(&original),sizeof original)),"short native records");need(original.layout==layout&&original.source==source&&original.hint==hint&&original.movement==m&&original.reverse==reverse,"native input record");r=original;} Record actual{layout,source,hint,m,reverse,{}};
  NativeNavGraph graph;for(unsigned a=0;a<4;++a){float x=positions[layout][a*2],y=positions[layout][a*2+1];graph.objects.push_back({a,{x-8,y-8,x+8,y+8}});}
  for(unsigned axis=0;axis<2;++axis){auto&edges=axis?graph.yEdges:graph.xEdges;for(unsigned a=0;a<4;++a)for(bool maximum:{false,true})edges.push_back({a,maximum});
   const auto value=[&](auto e){auto b=graph.objects[e.object].bounds;return axis?(e.maximum?b.maxY:b.minY):(e.maximum?b.maxX:b.minX);};
   std::sort(edges.begin(),edges.end(),[&](auto a,auto b){float x=value(a),y=value(b);unsigned ai=a.object*4+axis+(a.maximum?2:0),bi=b.object*4+axis+(b.maximum?2:0);return x!=y?x<y:reverse?ai>bi:ai<bi;});}
  auto b=graph.objects[source].bounds;b.minX+=movement[m][0];b.maxX+=movement[m][0];b.minY+=movement[m][1];b.maxY+=movement[m][1];
  auto ordinary=graph;updateNativeNavigationObject(ordinary,source,b);updateNativeContactGraphObject(graph,source,b,hint);
  bool bad=false,different=false;for(unsigned axis=0;axis<2;++axis){const auto& edges=axis?graph.yEdges:graph.xEdges;const auto& other=axis?ordinary.yEdges:ordinary.xEdges;
   for(unsigned i=0;i<8;++i){const auto e=edges[i];const unsigned id=e.object*4+axis+(e.maximum?2:0);actual.edges[axis*8+i]=id;if(argc==2)bad|=id!=r.edges[axis*8+i];different|=e.object!=other[i].object||e.maximum!=other[i].maximum;}}
  if(bad&&failures<5)std::cout<<"Mismatch layout/source/hint/movement/reverse "<<layout<<'/'<<source<<'/'<<hint<<'/'<<m<<'/'<<reverse<<'\n';failures+=bad;changed+=different;++count;const auto* bytes=reinterpret_cast<const unsigned char*>(&actual);for(unsigned i=0;i<sizeof actual;++i){fingerprint^=bytes[i];fingerprint*=1099511628211ull;}
 }
 if(argc==2){char extra;need(!in.read(&extra,1),"extra original records");}need(fingerprint==0x33ef8e241985f805ull,"unchanged-original graph producer fingerprint");need(count==1728&&changed==55,"native producer case coverage");std::cout<<count<<" original40e870/40ea10 graph repairs, "<<failures<<" mismatches, "<<changed<<" histories differ from unhinted update\n";return failures!=0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
