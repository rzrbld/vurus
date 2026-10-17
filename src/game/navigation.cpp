#include "game/navigation.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>
#include <unordered_map>

namespace vyruz {
struct NavigationSearch::Impl {
    struct Node {float cost=std::numeric_limits<float>::infinity();int64_t parent=-1;bool closed=false;};
    struct Entry {float estimate,cost;int64_t index;bool operator<(const Entry& b)const{return estimate>b.estimate||(estimate==b.estimate&&index>b.index);}};
    ContentPosition start,goal;
    float radius;
    int width,height;
    size_t limit;
    bool initialized=false;
    NavigationRoute result{RouteStatus::Pending,{},0};
    std::unordered_map<int64_t,Node> nodes;
    struct Queue:std::priority_queue<Entry> {
        const std::vector<Entry>& entries() const { return c; }
        void assign(std::vector<Entry> entries) { c=std::move(entries); }
    };
    Queue open;
    int64_t id(int x,int y)const{return static_cast<int64_t>(y)*width+x;}
    ContentPosition pos(int64_t n)const{return {static_cast<float>((n%width)*32+16),static_cast<float>((n/width)*16+8)};}
    static float distance(ContentPosition a,ContentPosition b){return std::sqrt(.625f*(a.x-b.x)*(a.x-b.x)+2.5f*(a.y-b.y)*(a.y-b.y));}
};
NavigationSearch::NavigationSearch():impl_(std::make_shared<Impl>()) {
    impl_->start={};impl_->goal={};impl_->radius=0;impl_->width=0;impl_->height=0;impl_->limit=0;
    impl_->result.status=RouteStatus::InvalidInput;
}
NavigationSearch::NavigationSearch(const NavigationSearch& other):impl_(std::make_shared<Impl>(*other.impl_)) {}
NavigationSearch& NavigationSearch::operator=(const NavigationSearch& other) {
    if(this!=&other)impl_=std::make_shared<Impl>(*other.impl_);
    return *this;
}
NavigationSearchState NavigationSearch::capture() const {
    const auto& s=*impl_;
    NavigationSearchState result;
    result.start=s.start;result.goal=s.goal;result.radius=s.radius;
    result.width=s.width;result.height=s.height;result.limit=s.limit;
    result.initialized=s.initialized;result.result=s.result;
    for(const auto& [index,n]:s.nodes)result.nodes.push_back({index,n.cost,n.parent,n.closed});
    std::sort(result.nodes.begin(),result.nodes.end(),[](const auto& a,const auto& b){return a.index<b.index;});
    for(const auto& e:s.open.entries())result.open.push_back({e.estimate,e.cost,e.index});
    return result;
}
bool NavigationSearch::restore(const Level& level,const NavigationSearchState& saved) {
    auto valid=[&](ContentPosition p){return std::isfinite(p.x)&&std::isfinite(p.y)&&p.x>=0&&p.y>=0&&p.x<level.mapW*64.f&&p.y<level.mapH*32.f;};
    if(level.mapW<=0 || level.mapH<=0 || level.mapW>32767 || level.mapH>32767 ||
       saved.width!=level.mapW*2 || saved.height!=level.mapH*2 || !valid(saved.start) || !valid(saved.goal) ||
       !std::isfinite(saved.radius) || saved.radius<0 || saved.result.expanded>saved.limit ||
       static_cast<unsigned>(saved.result.status)>static_cast<unsigned>(RouteStatus::Pending))return false;
    const int64_t count=static_cast<int64_t>(saved.width)*saved.height;
    if(saved.nodes.size()>static_cast<uint64_t>(count) || saved.result.points.size()>saved.nodes.size()+1 ||
       (!saved.initialized && (!saved.nodes.empty() || !saved.open.empty() || saved.result.expanded || saved.result.status!=RouteStatus::Pending)) ||
       (saved.result.status!=RouteStatus::Found && !saved.result.points.empty()))return false;
    auto restored=std::make_shared<Impl>();auto& s=*restored;
    s.start=saved.start;s.goal=saved.goal;s.radius=saved.radius;s.width=saved.width;s.height=saved.height;
    s.limit=saved.limit;s.initialized=saved.initialized;s.result=saved.result;
    size_t closed=0;
    for(const auto& n:saved.nodes) {
        if(n.index<0 || n.index>=count || !std::isfinite(n.cost) || n.cost<0 ||
           n.parent < -1 || n.parent>=count || !s.nodes.emplace(n.index,Impl::Node{n.cost,n.parent,n.closed}).second)return false;
        closed+=n.closed;
    }
    if(closed!=saved.result.expanded)return false;
    // Every parent has strictly lower cost, which also excludes cycles.
    for(const auto& [index,n]:s.nodes)if(n.parent!=-1) {
        const auto p=s.nodes.find(n.parent);
        if(p==s.nodes.end() || p->second.cost>=n.cost || !p->second.closed)return false;
    }
    std::vector<Impl::Entry> entries;entries.reserve(saved.open.size());
    for(const auto& e:saved.open) {
        if(!std::isfinite(e.cost) || !std::isfinite(e.estimate) || e.cost<0 || e.estimate<e.cost || !s.nodes.count(e.index))return false;
        entries.push_back({e.estimate,e.cost,e.index});
    }
    if(!std::is_heap(entries.begin(),entries.end()))return false;
    for(const auto p:s.result.points)if(!valid(p))return false;
    s.open.assign(std::move(entries));impl_=std::move(restored);return true;
}
NavigationSearch::NavigationSearch(const Level& level,ContentPosition start,ContentPosition goal,float radius,size_t limit)
    :impl_(std::make_shared<Impl>()) {
    auto& s=*impl_;s.start=start;s.goal=goal;s.radius=radius;s.width=0;s.height=0;s.limit=limit;
    auto valid=[&](ContentPosition p){return std::isfinite(p.x)&&std::isfinite(p.y)&&p.x>=0&&p.y>=0&&p.x<level.mapW*64.f&&p.y<level.mapH*32.f;};
    if(level.mapW<=0||level.mapH<=0||level.mapW>32767||level.mapH>32767||!valid(start)||!valid(goal)||!std::isfinite(radius)||radius<0)
        s.result.status=RouteStatus::InvalidInput;
    else {s.width=level.mapW*2;s.height=level.mapH*2;}
}
NavigationRoute NavigationSearch::advance(const Level& level,const RouteBlocked& blocked,size_t budget) {
    auto& s=*impl_;
    if(s.result.status!=RouteStatus::Pending || !budget)return s.result;
    auto clear=[&](ContentPosition a,ContentPosition b){return terrainMoveAllowed(level,a.x,a.y,b.x,b.y,s.radius)&&(!blocked||!blocked(a,b,s.radius));};
    if(!s.initialized) {
        s.initialized=true;
        if(clear(s.start,s.goal)){s.result.status=RouteStatus::Found;s.result.points.push_back(s.goal);return s.result;}
        if(!s.limit){s.result.status=RouteStatus::SearchLimit;return s.result;}
        const int sx=static_cast<int>(s.start.x/32),sy=static_cast<int>(s.start.y/16);
        for(int y=std::max(0,sy-1);y<=std::min(s.height-1,sy+1);++y)for(int x=std::max(0,sx-1);x<=std::min(s.width-1,sx+1);++x){
            const auto n=s.id(x,y);const auto p=s.pos(n);if(!clear(s.start,p))continue;
            auto& node=s.nodes[n];node.cost=s.distance(s.start,p);s.open.push({node.cost+s.distance(p,s.goal),node.cost,n});
        }
    }
    size_t work=0;
    while(!s.open.empty() && work<budget) {
        const auto entry=s.open.top();s.open.pop();++work; // stale entries also consume budget
        auto& current=s.nodes[entry.index];if(current.closed||entry.cost!=current.cost)continue;
        if(s.result.expanded>=s.limit){s.result.status=RouteStatus::SearchLimit;return s.result;}
        current.closed=true;++s.result.expanded;
        const auto p=s.pos(entry.index);const float cost=current.cost;
        if(std::abs(p.x-s.goal.x)<=48&&std::abs(p.y-s.goal.y)<=24&&clear(p,s.goal)) {
            s.result.status=RouteStatus::Found;s.result.points.push_back(s.goal);
            for(int64_t n=entry.index;n!=-1;n=s.nodes[n].parent)s.result.points.push_back(s.pos(n));
            std::reverse(s.result.points.begin(),s.result.points.end());return s.result;
        }
        const int x=static_cast<int>(entry.index%s.width),y=static_cast<int>(entry.index/s.width);
        for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx) {
            if((dx==0&&dy==0)||x+dx<0||x+dx>=s.width||y+dy<0||y+dy>=s.height)continue;
            const auto next=s.id(x+dx,y+dy);const auto q=s.pos(next);
            if(dx&&dy) {
                const auto a=s.pos(s.id(x+dx,y)),b=s.pos(s.id(x,y+dy));
                if(!clear(p,a)||!clear(p,b)||!clear(a,q)||!clear(b,q))continue;
            }
            if(!clear(p,q))continue;
            const float proposed=cost+s.distance(p,q);auto& node=s.nodes[next];
            if(node.closed||proposed>=node.cost)continue;
            node.cost=proposed;node.parent=entry.index;s.open.push({proposed+s.distance(q,s.goal),proposed,next});
        }
    }
    if(s.open.empty())s.result.status=RouteStatus::Unreachable;
    return s.result;
}
NavigationRoute findRoute(const Level& level,ContentPosition start,ContentPosition goal,
    float radius,const RouteBlocked& blocked,size_t maximumExpanded) {
    NavigationSearch search(level,start,goal,radius,maximumExpanded);
    NavigationRoute result;
    do {result=search.advance(level,blocked,std::max(size_t{1},maximumExpanded));}while(result.status==RouteStatus::Pending);
    return result;
}
} // namespace vyruz
