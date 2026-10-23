#include "game/native_contact_graph.h"
#include <iostream>
#include <random>
#include <stdexcept>
using namespace vyruz;
namespace reference { using namespace vyruz;
namespace {
float graphCoordinate(const NativeNavGraph& g,NativeNavGraphEdge e,bool x){auto b=g.objects[e.object].bounds;return x?(e.maximum?b.maxX:b.minX):(e.maximum?b.maxY:b.minY);}
void reorderGraphEdge(NativeNavGraph& g,NativeNavGraphAxis& edges,bool x,size_t object,bool maximum){
 const auto n=edges.endpoint(object,maximum);if(n==NativeNavGraphAxis::none)return;
 const float value=graphCoordinate(g,edges.edge(n),x);
 for(auto p=edges.previous(n);p!=NativeNavGraphAxis::none&&graphCoordinate(g,edges.edge(p),x)>value;p=edges.previous(n))edges.moveBefore(n,p);
 for(auto q=edges.next(n);q!=NativeNavGraphAxis::none&&graphCoordinate(g,edges.edge(q),x)<value;q=edges.next(n))edges.moveBefore(q,n);
}
size_t allocateGraphObject(NativeNavGraph& g,NativeNavGraphObject object){
 if(g.indexedObjectCount!=g.objects.size()){
  g.freeObjects.clear();for(size_t i=0;i<g.objects.size();++i)if(!g.objects[i].alive)g.freeObjects.push_back(i);
  std::make_heap(g.freeObjects.begin(),g.freeObjects.end(),std::greater<size_t>());g.indexedObjectCount=g.objects.size();
 }
 size_t id;if(g.freeObjects.empty()){id=g.objects.size();g.objects.push_back(object);g.indexedObjectCount=g.objects.size();}
 else{std::pop_heap(g.freeObjects.begin(),g.freeObjects.end(),std::greater<size_t>());id=g.freeObjects.back();g.freeObjects.pop_back();g.objects[id]=object;}
 return id;
}
void insertGraphEdge(NativeNavGraph& g,NativeNavGraphAxis& edges,bool x,NativeNavGraphEdge e){
 const float c=graphCoordinate(g,e,x);auto n=edges.first();
 while(n!=NativeNavGraphAxis::none&&!(c<=graphCoordinate(g,edges.edge(n),x)))n=edges.next(n);
 edges.insertBefore(n,e);
}
}
NativeNavPoint insertNativeNavigationPoint(NativeNavGraph& g,NativeNavPoint p){
 p.graphNode=allocateGraphObject(g,{0,{p.x,p.y,p.x,p.y},true,true});
 for(bool x:{true,false})insertGraphEdge(g,x?g.xEdges:g.yEdges,x,{p.graphNode,false});return p;
}
size_t insertNativeNavigationObject(NativeNavGraph& g,uint64_t reference,NativeNavObstacle b){
 const auto id=allocateGraphObject(g,{reference,b,true});
 for(bool maximum:{true,false})for(bool x:{true,false})insertGraphEdge(g,x?g.xEdges:g.yEdges,x,{id,maximum});
 return id;
}
void updateNativeNavigationObject(NativeNavGraph& g,size_t id,NativeNavObstacle b){
 if(id>=g.objects.size()||!g.objects[id].alive)return;
 const auto old=g.objects[id].bounds;
 if(old.minX==b.minX&&old.minY==b.minY&&old.maxX==b.maxX&&old.maxY==b.maxY)return;
 g.objects[id].bounds=b;
 // Write all four coordinates first; retain40e630's endpoint selection and
 // its unordered head-min/tail-max histories.
 for(bool x:{true,false}){auto& edges=x?g.xEdges:g.yEdges;
  const auto lo=edges.endpoint(id,false),hi=edges.endpoint(id,true);if(lo==NativeNavGraphAxis::none||hi==NativeNavGraphAxis::none)continue;
  const auto ordered=[&](size_t n){const auto p=edges.previous(n),q=edges.next(n);const float c=graphCoordinate(g,edges.edge(n),x);return(p==NativeNavGraphAxis::none||graphCoordinate(g,edges.edge(p),x)<=c)&&(q==NativeNavGraphAxis::none||c<=graphCoordinate(g,edges.edge(q),x));};
  if(lo!=edges.first()&&!ordered(lo)){reorderGraphEdge(g,edges,x,id,false);reorderGraphEdge(g,edges,x,id,true);}
  else if(hi!=edges.last()&&!ordered(hi)){reorderGraphEdge(g,edges,x,id,true);reorderGraphEdge(g,edges,x,id,false);}
 }
}
void removeNativeNavigationObject(NativeNavGraph& g,size_t id){
 if(id>=g.objects.size()||!g.objects[id].alive)return;
 const bool indexed=g.indexedObjectCount==g.objects.size();g.objects[id].alive=false;
 for(auto* edges:{&g.xEdges,&g.yEdges}){edges->eraseNode(edges->endpoint(id,false));edges->eraseNode(edges->endpoint(id,true));}
 if(indexed){g.freeObjects.push_back(id);std::push_heap(g.freeObjects.begin(),g.freeObjects.end(),std::greater<size_t>());}
}
}

namespace {
unsigned controls=0;
void need(bool ok,const char* s){++controls;if(!ok)throw std::runtime_error(s);}
void compare(const NativeNavGraph& a,const NativeNavGraph& b){
 need(a.objects.size()==b.objects.size(),"object count");
 for(size_t i=0;i<a.objects.size();++i){const auto& x=a.objects[i];const auto& y=b.objects[i];need(x.reference==y.reference&&x.alive==y.alive&&x.point==y.point&&x.bounds.minX==y.bounds.minX&&x.bounds.maxX==y.bounds.maxX&&x.bounds.minY==y.bounds.minY&&x.bounds.maxY==y.bounds.maxY,"object/slot/bounds identity");}
 for(bool x:{true,false}){const auto& p=x?a.xEdges:a.yEdges;const auto& q=x?b.xEdges:b.yEdges;need(p.size()==q.size(),"axis size");auto j=q.begin();for(auto i=p.begin();i!=p.end();++i,++j)need(i->object==j->object&&i->maximum==j->maximum,"complete exact linked axis order");}
 need(validNativeNavigationGraph(a)&&validNativeNavigationGraph(b),"valid graph");
}
}
int main()try{
 // Head-first reference is the explicit v1–35 save compatibility policy.
 // Fresh native pivot histories are independently covered by native_graph_pivot.

 // Contact-assisted bounds may remain in order while an endpoint becomes
 // equal to the cached insertion coordinate. The next insertion must still
 // stop at that endpoint, before the previous point, on either axis.
 for(bool x:{true,false}){
  NativeNavGraph actual,expected;actual.xEdges.useLegacyInsertionPolicy();actual.yEdges.useLegacyInsertionPolicy();
  const auto anchor=insertNativeNavigationObject(actual,1,{-20,-20,-15,-15});reference::insertNativeNavigationObject(expected,1,{-20,-20,-15,-15});
  const auto mover=insertNativeNavigationObject(actual,2,{-5,-5,5,5});reference::insertNativeNavigationObject(expected,2,{-5,-5,5,5});
  insertNativeNavigationPoint(actual,{10,10});reference::insertNativeNavigationPoint(expected,{10,10});
  const NativeNavObstacle bounds=x?NativeNavObstacle{0,-5,10,5}:NativeNavObstacle{-5,0,5,10};
  updateNativeContactGraphObject(actual,mover,bounds,anchor);updateNativeContactGraphObject(expected,mover,bounds,anchor);
  const NativeNavPoint next=x?NativeNavPoint{10,11}:NativeNavPoint{11,10};
  insertNativeNavigationPoint(actual,next);reference::insertNativeNavigationPoint(expected,next);compare(actual,expected);
 }
 NativeNavGraph a,b;a.xEdges.useLegacyInsertionPolicy();a.yEdges.useLegacyInsertionPolicy();std::mt19937 random(0x40e630);std::vector<size_t> live;
 for(unsigned tick=0;tick<10000;++tick){
  const unsigned action=random()%8;
  if(live.empty()||action<4){
   float x=float(int(random()%101)-50),y=float(int(random()%101)-50);if(tick%7==0)x=y=0;
   if(action%2){auto p=insertNativeNavigationPoint(a,{x,y});auto q=reference::insertNativeNavigationPoint(b,{x,y});need(p.graphNode==q.graphNode,"point slot identity");live.push_back(p.graphNode);}
   else{NativeNavObstacle bounds{x,y,x+float(random()%40),y+float(random()%40)};const auto p=insertNativeNavigationObject(a,tick,bounds);const auto q=reference::insertNativeNavigationObject(b,tick,bounds);need(p==q,"body slot identity");live.push_back(p);}
  }else{
   const auto n=random()%live.size(),id=live[n];
   if(action<6&&!a.objects[id].point){float x=float(int(random()%151)-75),y=float(int(random()%151)-75);NativeNavObstacle bounds{x,y,x+float(random()%70),y+float(random()%70)};
    std::optional<size_t> hint;
    if(tick%2)for(const auto candidate:live)if(candidate!=id&&!a.objects[candidate].point){hint=candidate;break;}
    if(hint){updateNativeContactGraphObject(a,id,bounds,hint);updateNativeContactGraphObject(b,id,bounds,hint);}
    else{updateNativeNavigationObject(a,id,bounds);reference::updateNativeNavigationObject(b,id,bounds);}}
   else{removeNativeNavigationObject(a,id);reference::removeNativeNavigationObject(b,id);live.erase(live.begin()+n);}
  }
  compare(a,b);
  if(tick%101==0){auto copy=a;compare(copy,b);a=std::move(copy);}
  if(tick%113==0&&!a.xEdges.empty()){a.xEdges.front()=static_cast<const NativeNavGraphAxis&>(b.xEdges).front();a.yEdges.front()=static_cast<const NativeNavGraphAxis&>(b.yEdges).front();}
 }
 // Explicit native head-min disorder: moving a head minimum need not reorder it.
 NativeNavGraph c,d;c.xEdges.useLegacyInsertionPolicy();c.yEdges.useLegacyInsertionPolicy();auto x=insertNativeNavigationObject(c,1,{0,0,100,100});reference::insertNativeNavigationObject(d,1,{0,0,100,100});insertNativeNavigationObject(c,2,{1,10,10,20});reference::insertNativeNavigationObject(d,2,{1,10,10,20});
 updateNativeNavigationObject(c,x,{2,0,102,100});reference::updateNativeNavigationObject(d,x,{2,0,102,100});
 for(float v:{1.f,1.f,3.f,7.f,-4.f,100.f,100.f,99.f}){insertNativeNavigationPoint(c,{v,v});reference::insertNativeNavigationPoint(d,{v,v});compare(c,d);}
 std::cout<<"PASS "<<controls<<" legacy insertion prefix/order/update/reuse/copy controls\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
