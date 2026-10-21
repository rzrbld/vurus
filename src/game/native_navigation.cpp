#include "game/native_navigation.h"
#include <algorithm>
#include <cmath>
namespace vyruz {
namespace {
// 40fa8c..40faae / 40fce0: store the float slope while retaining the
// extended quotient for the intercept. 40fc21..40fc2d / 40ff00 spill only
// after evaluating the intersection from those two stored coefficients.
struct NativeNavLine {
 float slope,intercept;
 float at(float coordinate) const {return float((long double)coordinate*slope+intercept);}
};
NativeNavLine nativeNavLine(float start,float otherStart,float end,float otherEnd){
 if(start==end)return {0,otherEnd};
 const long double slope=((long double)otherEnd-otherStart)/((long double)end-start);
 return {float(slope),float((long double)otherEnd-slope*end)};
}
unsigned corners(unsigned b){return ((b&0x14)==0x14?1:0)|((b&0x24)==0x24?2:0)|((b&0x18)==0x18?4:0)|((b&0x28)==0x28?8:0);}
float distance2(NativeNavPoint a,NativeNavPoint b){float x=a.x-b.x,y=a.y-b.y;return x*x+y*y;}
}
std::vector<NativeNavPoint> nativeNavigationCorners(NativeNavPoint a,NativeNavPoint b,NativeNavPoint h,NativeNavObstacle o){
 const float lx=o.minX-h.x-8,ly=o.minY-h.y-8,rx=o.maxX+h.x+8,ry=o.maxY+h.y+8;
 unsigned side=0,mask=0;
 if(a.x>lx&&a.x<=o.minX)side=0x10;else if(a.x<rx&&a.x>=o.maxX)side=0x20;
 if(a.y>ly&&a.y<=o.minY)side|=4;else if(a.y<ry&&a.y>=o.maxY)side|=8;
 auto classify=[&](NativeNavPoint p){unsigned r=0;if(p.x<lx||p.x>rx)r=2;if(p.y<ly||p.y>ry)r|=1;if(p.x<o.minX)r|=0x10;else if(p.x>o.maxX)r|=0x20;if(p.y<o.minY)r|=4;else if(p.y>o.maxY)r|=8;return r;};
 if((side&0x30)&&(side&0xc))mask=corners(side);
 else {
  unsigned u=classify(a),v=classify(b);
  if(u&3){v=u&0x3c;if(!(u&0x30))v|=0x30;if(!(v&0xc))v|=0xc;mask=corners(v);}
  else if(v&3){
   if((v&~3u)==u)mask=corners(v);
   else if(u==4||u==8){v&=0x30;if(!v)v=0x30;mask=corners(u|v);}
   else if(u==0x10||u==0x20){v&=0xc;if(!v)v=0xc;mask=corners(u|v);}
   else {if((u&0x10)&&(v&0x20))mask=corners((u&0xc)|0x20);else if((u&0x20)&&(v&0x10))mask=corners((u&0xc)|0x10);if((u&4)&&(v&8))mask|=corners((u&0x30)|8);else if((u&8)&&(v&4))mask|=corners((u&0x30)|4);}
  }
 }
 std::vector<NativeNavPoint> r;if(mask&1)r.push_back({lx,ly});if(mask&2)r.push_back({rx,ly});if(mask&4)r.push_back({lx,ry});if(mask&8)r.push_back({rx,ry});return r;
}
std::vector<NativeNavPoint> nativeNavigationCornersExact(NativeNavPoint a,NativeNavPoint b,NativeNavPoint h,NativeNavObstacle o){
 // Original40f322 retains the X margin in x87, but40f362 spills Y first.
 const long double mx=(long double)h.x+8; const float my=float((long double)h.y+8);
 const float lx=float((long double)o.minX-mx),ly=float((long double)o.minY-my),rx=float(mx+o.maxX),ry=float((long double)my+o.maxY);
 unsigned side=0,mask=0;
 if(a.x>lx&&a.x<=o.minX)side=0x10;else if(a.x<rx&&a.x>=o.maxX)side=0x20;
 if(a.y>ly&&a.y<=o.minY)side|=4;else if(a.y<ry&&a.y>=o.maxY)side|=8;
 auto classify=[&](NativeNavPoint p){unsigned r=0;if(p.x<lx||p.x>rx)r=2;if(p.y<ly||p.y>ry)r|=1;if(p.x<o.minX)r|=0x10;else if(p.x>o.maxX)r|=0x20;if(p.y<o.minY)r|=4;else if(p.y>o.maxY)r|=8;return r;};
 if((side&0x30)&&(side&0xc))mask=corners(side);
 else {
  unsigned u=classify(a),v=classify(b);
  if(u&3){v=u&0x3c;if(!(u&0x30))v|=0x30;if(!(v&0xc))v|=0xc;mask=corners(v);}
  else if(v&3){
   if((v&~3u)==u)mask=corners(v);
   else if(u==4||u==8){v&=0x30;if(!v)v=0x30;mask=corners(u|v);}
   else if(u==0x10||u==0x20){v&=0xc;if(!v)v=0xc;mask=corners(u|v);}
   else {if((u&0x10)&&(v&0x20))mask=corners((u&0xc)|0x20);else if((u&0x20)&&(v&0x10))mask=corners((u&0xc)|0x10);if((u&4)&&(v&8))mask|=corners((u&0x30)|8);else if((u&8)&&(v&4))mask|=corners((u&0x30)|4);}
  }
 }
 // Output40f54a..5fc is separately rounded after each complete expression.
 std::vector<NativeNavPoint> r;const float outLx=float((long double)o.minX-h.x-8),outLy=float((long double)o.minY-h.y-8),outRx=float((long double)o.maxX+h.x+8),outRy=float((long double)o.maxY+h.y+8);if(mask&1)r.push_back({outLx,outLy});if(mask&2)r.push_back({outRx,outLy});if(mask&4)r.push_back({outLx,outRy});if(mask&8)r.push_back({outRx,outRy});return r;
}
NativeNavState beginNativeNavigation(NativeNavPoint a,NativeNavPoint b,NativeNavPoint h,float maximumDistance,const NativeNavPointFactory& createPoint){if(createPoint){a=createPoint(a);b=createPoint(b);}return {{{{a,b},0}},{b,a},h,maximumDistance};}
NativeNavProgress advanceNativeNavigation(NativeNavState& s,const NativeNavQuery& query,const NativeNavPointFactory& createPoint,bool nativeCornerPrecision){
 const size_t count=s.candidates.size();std::vector<NativeNavCandidate> kept,added;bool worked=false,complete=false;
 for(size_t n=0;n<count;++n){auto c=s.candidates[n];
  if(c.points.empty()||c.cursor>=c.points.size())continue;
  if(c.cursor+1==c.points.size()){complete=true;kept.push_back(std::move(c));continue;}
  worked=true;auto a=c.points[c.cursor],b=c.points[c.cursor+1];if(a==b)continue;
  bool overlap=false;std::optional<NativeNavObstacle> hit;
  if(c.cursor){hit=query(a,b,s.halfExtent,true);overlap=hit.has_value();}
  if(!hit)hit=query(a,b,s.halfExtent,false);
  if(!hit){++c.cursor;if(c.cursor+1==c.points.size())complete=true;kept.push_back(std::move(c));continue;}
  auto points=nativeCornerPrecision?nativeNavigationCornersExact(a,b,s.halfExtent,*hit):nativeNavigationCorners(a,b,s.halfExtent,*hit);
  for(auto it=points.rbegin();it!=points.rend();++it){auto p=*it;
   if(s.maximumDistance>0){
    const float limit=s.maximumDistance*s.maximumDistance;
    if(nativeCornerPrecision){
     // 4102cf stores the radius square as float;4102f3..30b keeps point
     // subtraction, both squares and their sum in x87 until comparison.
     const long double x=(long double)c.points.front().x-p.x,y=(long double)c.points.front().y-p.y;
     if(x*x+y*y>limit)continue;
    }else if(distance2(c.points.front(),p)>limit)continue;
   }
   if(std::find(s.seen.begin()+std::min<size_t>(1,s.seen.size()),s.seen.end(),p)!=s.seen.end())continue;
   if(createPoint)p=createPoint(p);
   s.seen.push_back(p);auto branch=c;
   if(overlap&&branch.cursor+2<branch.points.size())branch.points.erase(branch.points.begin()+branch.cursor+1);
   branch.points.insert(branch.points.begin()+branch.cursor+1,p);added.push_back(std::move(branch));
   if(c.cursor+2<c.points.size()){NativeNavCandidate shortBranch;shortBranch.points.assign(c.points.begin(),c.points.begin()+c.cursor+1);shortBranch.cursor=c.cursor;shortBranch.points.push_back(p);shortBranch.points.push_back(c.points.back());added.push_back(std::move(shortBranch));}
  }
 }
 kept.insert(kept.end(),std::make_move_iterator(added.begin()),std::make_move_iterator(added.end()));s.candidates=std::move(kept);
 if(worked)return complete?NativeNavProgress::Found:NativeNavProgress::Working;
 return s.candidates.empty()?NativeNavProgress::Failed:NativeNavProgress::Complete;
}
std::optional<NativeNavCandidate> selectNativeNavigation(NativeNavState& s,const NativeNavQuery& query){
 std::optional<NativeNavCandidate> best;float score=0;
 for(auto c:s.candidates){if(c.points.empty()||c.cursor+1!=c.points.size())continue;
  for(size_t i=0;i+1<c.points.size();++i){size_t far=i+1;for(size_t j=i+2;j<c.points.size();++j)if(!query(c.points[i],c.points[j],s.halfExtent,false))far=j;if(far>i+1)c.points.erase(c.points.begin()+i+1,c.points.begin()+far);}
  long double value=0;for(size_t i=1;i<c.points.size();++i){long double x=(long double)c.points[i].x-c.points.front().x,y=(long double)c.points[i].y-c.points.front().y;value+=x*x+y*y;}
  if(!best||float(value)<score){score=float(value);c.cursor=0;best=std::move(c);}
 }
 s.candidates.clear();if(best)s.candidates.push_back(*best);return best;
}
}
namespace vyruz {
std::optional<NativeNavObstacle> queryNativeNavigation(const std::vector<NativeNavObstacle>& obstacles,NativeNavPoint a,NativeNavPoint b,NativeNavPoint h,bool overlap){
 struct Edge {float coordinate;size_t obstacle;int boundary;};
 auto earlier=[](Edge l,Edge r){if(l.coordinate!=r.coordinate)return l.coordinate<r.coordinate;if(l.obstacle!=r.obstacle)return l.obstacle>r.obstacle;return l.boundary>r.boundary;};
 auto eachEdge=[&](bool x,const auto& visit){for(size_t i=0;i<obstacles.size();++i){const auto o=obstacles[i];visit(Edge{x?o.maxX:o.maxY,i,0});visit(Edge{x?o.minX:o.minY,i,1});}};
 if(overlap){std::optional<Edge> before,after;eachEdge(true,[&](Edge edge){const auto o=obstacles[edge.obstacle];if(!(o.minX<=a.x+h.x&&a.x-h.x<=o.maxX&&o.minY<=a.y+h.y&&a.y-h.y<=o.maxY))return;if(edge.coordinate<a.x&&edge.coordinate>=a.x-h.x){if(!before||earlier(*before,edge))before=edge;}else if(edge.coordinate>=a.x&&edge.coordinate<=a.x+h.x){if(!after||earlier(edge,*after))after=edge;}});if(before)return obstacles[before->obstacle];if(after)return obstacles[after->obstacle];return {};}

 if(a==b)return {};
 struct Hit {size_t id;NativeNavPoint point;};
 auto scan=[&](bool x)->std::optional<Hit>{const auto line=x?nativeNavLine(a.x,a.y,b.x,b.y):nativeNavLine(a.y,a.x,b.y,b.x);float start=x?a.x:a.y,end=x?b.x:b.y;bool increasing=end>start;std::optional<Edge> best;NativeNavPoint bestPoint;eachEdge(x,[&](Edge edge){float c=edge.coordinate;if(increasing?(c<start||c>end):(c>=start||c<end))return;if(best&&(increasing?!earlier(edge,*best):!earlier(*best,edge)))return;NativeNavPoint p;if(x){p.x=c;p.y=line.at(c);}else{p.y=c;p.x=line.at(c);}auto o=obstacles[edge.obstacle];if(p.x<std::min(a.x,b.x)||p.x>std::max(a.x,b.x)||p.y<std::min(a.y,b.y)||p.y>std::max(a.y,b.y)||p.x<o.minX||p.x>o.maxX||p.y<o.minY||p.y>o.maxY)return;best=edge;bestPoint=p;});if(best)return Hit{best->obstacle,bestPoint};return {};};

 // 40f9b0 falls back to Y for vertical, 40fce0 to X for horizontal.
 auto xhit=scan(a.x!=b.x),yhit=scan(a.y==b.y);
 if(!xhit)return yhit?std::optional<NativeNavObstacle>(obstacles[yhit->id]):std::nullopt;
 if(yhit&&xhit->id!=yhit->id&&distance2(yhit->point,a)<=distance2(xhit->point,a))return obstacles[yhit->id];
 return obstacles[xhit->id];
}
}
namespace vyruz {
namespace {
float graphCoordinate(const NativeNavGraph& g,NativeNavGraphEdge e,bool x){auto b=g.objects[e.object].bounds;return x?(e.maximum?b.maxX:b.minX):(e.maximum?b.maxY:b.minY);}
void reorderGraphEdge(NativeNavGraph& g,std::vector<NativeNavGraphEdge>& edges,bool x,size_t object,bool maximum){
 auto it=std::find_if(edges.begin(),edges.end(),[&](auto e){return e.object==object&&e.maximum==maximum;});if(it==edges.end())return;
 size_t n=it-edges.begin();const float value=graphCoordinate(g,*it,x);
 while(n&&graphCoordinate(g,edges[n-1],x)>value){std::swap(edges[n],edges[n-1]);--n;}
 while(n+1<edges.size()&&graphCoordinate(g,edges[n+1],x)<value){std::swap(edges[n],edges[n+1]);++n;}
}
}
NativeNavPoint insertNativeNavigationPoint(NativeNavGraph& g,NativeNavPoint p){
 p.graphNode=size_t(std::find_if(g.objects.begin(),g.objects.end(),[](auto o){return !o.alive;})-g.objects.begin());
 NativeNavGraphObject object{0,{p.x,p.y,p.x,p.y},true,true};if(p.graphNode==g.objects.size())g.objects.push_back(object);else g.objects[p.graphNode]=object;
 for(bool x:{true,false}){auto& edges=x?g.xEdges:g.yEdges;NativeNavGraphEdge edge{p.graphNode,false};float c=x?p.x:p.y;auto it=std::find_if(edges.begin(),edges.end(),[&](auto e){return c<=graphCoordinate(g,e,x);});edges.insert(it,edge);}return p;
}
size_t insertNativeNavigationObject(NativeNavGraph& g,uint64_t reference,NativeNavObstacle b){
 size_t id=size_t(std::find_if(g.objects.begin(),g.objects.end(),[](auto o){return !o.alive;})-g.objects.begin());
 NativeNavGraphObject object{reference,b,true};if(id==g.objects.size())g.objects.push_back(object);else g.objects[id]=object;
 for(bool maximum:{true,false})for(bool x:{true,false}){auto& edges=x?g.xEdges:g.yEdges;NativeNavGraphEdge edge{id,maximum};float c=graphCoordinate(g,edge,x);auto it=std::find_if(edges.begin(),edges.end(),[&](auto e){return c<=graphCoordinate(g,e,x);});edges.insert(it,edge);}
 return id;
}
void updateNativeNavigationObject(NativeNavGraph& g,size_t id,NativeNavObstacle b){
 if(id>=g.objects.size()||!g.objects[id].alive)return;
 const auto old=g.objects[id].bounds;
 if(old.minX==b.minX&&old.minY==b.minY&&old.maxX==b.maxX&&old.maxY==b.maxY)return;
 g.objects[id].bounds=b;
 // 40e630 picks first endpoint based on whether its old neighbours remain
 // sorted after ALL four coordinates have been written by40ef20.
 for(bool x:{true,false}){auto& edges=x?g.xEdges:g.yEdges;auto position=[&](bool maximum){return size_t(std::find_if(edges.begin(),edges.end(),[&](auto e){return e.object==id&&e.maximum==maximum;})-edges.begin());};
  size_t lo=position(false),hi=position(true);auto ordered=[&](size_t n){float c=graphCoordinate(g,edges[n],x);return(!n||graphCoordinate(g,edges[n-1],x)<=c)&&(n+1==edges.size()||c<=graphCoordinate(g,edges[n+1],x));};
  // Native min endpoint at head is considered already ordered; max endpoint
  // at tail likewise. This branch ordering matters when bounds cross nodes.
  if(lo!=0&&!ordered(lo)){reorderGraphEdge(g,edges,x,id,false);reorderGraphEdge(g,edges,x,id,true);}
  else if(hi+1!=edges.size()&&!ordered(hi)){reorderGraphEdge(g,edges,x,id,true);reorderGraphEdge(g,edges,x,id,false);}
 }
}
void removeNativeNavigationObject(NativeNavGraph& g,size_t id){if(id>=g.objects.size())return;g.objects[id].alive=false;for(auto* edges:{&g.xEdges,&g.yEdges})std::erase_if(*edges,[&](auto e){return e.object==id;});}
std::optional<NativeNavObstacle> queryNativeNavigation(const NativeNavGraph& g,NativeNavPoint a,NativeNavPoint b,NativeNavPoint h,bool overlap,const NativeNavEligible& eligible){
 auto allowed=[&](NativeNavGraphEdge e){return g.objects[e.object].alive&&!g.objects[e.object].point&&(!eligible||eligible(g.objects[e.object].reference));};
 auto sourceIndex=[&](const std::vector<NativeNavGraphEdge>& edges){
  if(a.graphNode==SIZE_MAX||a.graphNode>=g.objects.size()||!g.objects[a.graphNode].alive||!g.objects[a.graphNode].point)return edges.size();
  return size_t(std::find_if(edges.begin(),edges.end(),[&](auto e){return e.object==a.graphNode;})-edges.begin());
 };
 const size_t fromX=sourceIndex(g.xEdges),fromY=sourceIndex(g.yEdges);
 auto pointSide=[&](const std::vector<NativeNavGraphEdge>& edges,const NativeNavGraphEdge& edge,bool forward){
  size_t from=&edges==&g.xEdges?fromX:fromY;if(from==edges.size())return true;
  size_t at=&edge-edges.data();return forward?at>from:at<from;
 };
 // Native40f840/40f9b0/40fce0 begin at the source node's neighbour.
 // Preserve graph list history, including temporarily unordered head/tail bounds.
 if(overlap&&fromX<g.xEdges.size()){
  auto check=[&](NativeNavGraphEdge e)->std::optional<NativeNavObstacle>{
   const auto o=g.objects[e.object].bounds;
   if(allowed(e)&&o.minX<=a.x+h.x&&a.x-h.x<=o.maxX&&o.minY<=a.y+h.y&&a.y-h.y<=o.maxY)return o;
   return {};
  };
  for(size_t n=fromX;n>0;){const auto e=g.xEdges[--n];if(graphCoordinate(g,e,true)<a.x-h.x)break;if(auto hit=check(e))return hit;}
  for(size_t n=fromX+1;n<g.xEdges.size();++n){const auto e=g.xEdges[n];if(graphCoordinate(g,e,true)>a.x+h.x)break;if(auto hit=check(e))return hit;}
  return {};
 }
 if(overlap){for(auto it=g.xEdges.rbegin();it!=g.xEdges.rend();++it){float c=graphCoordinate(g,*it,true);if(c>a.x||!pointSide(g.xEdges,*it,false)||(c==a.x&&a.graphNode==SIZE_MAX))continue;if(c<a.x-h.x)break;auto o=g.objects[it->object].bounds;if(allowed(*it)&&o.minX<=a.x+h.x&&a.x-h.x<=o.maxX&&o.minY<=a.y+h.y&&a.y-h.y<=o.maxY)return o;}for(const auto& e:g.xEdges){float c=graphCoordinate(g,e,true);if(c<a.x||!pointSide(g.xEdges,e,true))continue;if(c>a.x+h.x)break;auto o=g.objects[e.object].bounds;if(allowed(e)&&o.minX<=a.x+h.x&&a.x-h.x<=o.maxX&&o.minY<=a.y+h.y&&a.y-h.y<=o.maxY)return o;}return {};}
 if(a==b)return {};
 struct Hit {size_t id;NativeNavPoint point;};
 auto scan=[&](bool x)->std::optional<Hit>{const auto line=x?nativeNavLine(a.x,a.y,b.x,b.y):nativeNavLine(a.y,a.x,b.y,b.x);const auto& edges=x?g.xEdges:g.yEdges;float start=x?a.x:a.y,end=x?b.x:b.y;bool increasing=end>start;
 auto test=[&](const NativeNavGraphEdge& edge)->std::optional<Hit>{float c=graphCoordinate(g,edge,x);if(increasing?(c<start||c>end):(c>start||c<end||(c==start&&a.graphNode==SIZE_MAX)))return {};if(!pointSide(edges,edge,increasing))return {};if(!allowed(edge))return {};NativeNavPoint p;if(x){p.x=c;p.y=line.at(c);}else{p.y=c;p.x=line.at(c);}auto o=g.objects[edge.object].bounds;if(p.x<std::min(a.x,b.x)||p.x>std::max(a.x,b.x)||p.y<std::min(a.y,b.y)||p.y>std::max(a.y,b.y)||p.x<o.minX||p.x>o.maxX||p.y<o.minY||p.y>o.maxY)return {};return Hit{edge.object,p};};
 const size_t source=x?fromX:fromY;
 if(source<edges.size()){
  if(increasing){for(size_t n=source+1;n<edges.size();++n){const auto& edge=edges[n];if(graphCoordinate(g,edge,x)>end)break;if(auto hit=test(edge))return hit;}}
  else {for(size_t n=source;n>0;){const auto& edge=edges[--n];if(graphCoordinate(g,edge,x)<end)break;if(auto hit=test(edge))return hit;}}
 }else if(increasing){for(const auto& edge:edges){if(graphCoordinate(g,edge,x)>end)break;if(auto hit=test(edge))return hit;}}
 else {for(auto it=edges.rbegin();it!=edges.rend();++it){if(graphCoordinate(g,*it,x)<end)break;if(auto hit=test(*it))return hit;}}
 return {};};
 auto xhit=scan(a.x!=b.x),yhit=scan(a.y==b.y);if(!xhit)return yhit?std::optional<NativeNavObstacle>(g.objects[yhit->id].bounds):std::nullopt;if(yhit&&xhit->id!=yhit->id&&distance2(yhit->point,a)<=distance2(xhit->point,a))return g.objects[yhit->id].bounds;return g.objects[xhit->id].bounds;
}
}
