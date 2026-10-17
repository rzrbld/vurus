#pragma once
#include "game/native_navigation.h"
#include <algorithm>
namespace vyruz {
// 40efd0/40e870/40ea10 repair, using the final410a00 non-contact DGO as
// insertion anchor. Equal coordinates retain the anchor's directed insertion
// history; sorting the complete axis would lose that history.
inline void updateNativeContactGraphObject(NativeNavGraph& graph,size_t source,
        NativeNavObstacle bounds,std::optional<size_t> hint) {
    if(source>=graph.objects.size()||!graph.objects[source].alive)return;
    if(!hint||*hint>=graph.objects.size()||*hint==source||!graph.objects[*hint].alive||graph.objects[*hint].point){
        updateNativeNavigationObject(graph,source,bounds);return;
    }
    const auto old=graph.objects[source].bounds;
    if(old.minX==bounds.minX&&old.minY==bounds.minY&&old.maxX==bounds.maxX&&old.maxY==bounds.maxY)return;
    graph.objects[source].bounds=bounds;
    for(bool x:{true,false}){
        auto& edges=x?graph.xEdges:graph.yEdges;
        const auto coordinate=[&](auto edge){auto b=graph.objects[edge.object].bounds;return x?(edge.maximum?b.maxX:b.minX):(edge.maximum?b.maxY:b.minY);};
        const auto position=[&](size_t object,bool maximum){return size_t(std::find_if(edges.begin(),edges.end(),[&](auto e){return e.object==object&&e.maximum==maximum;})-edges.begin());};
        const auto ordered=[&](size_t n){const auto c=coordinate(edges[n]);return (!n||coordinate(edges[n-1])<=c)&&(n+1==edges.size()||c<=coordinate(edges[n+1]));};
        const auto relocate=[&](bool maximum){
            const auto n=position(source,maximum);if(n>=edges.size())return;
            const auto edge=edges[n];edges.erase(edges.begin()+n);
            auto anchor=position(*hint,maximum);if(anchor>=edges.size()){edges.insert(edges.begin()+std::min(n,edges.size()),edge);return;}
            if(coordinate(edge)>coordinate(edges[anchor]))++anchor;
            edges.insert(edges.begin()+anchor,edge);
            // 40e790's strict adjacent swaps do not cross equal coordinates.
            while(anchor&&coordinate(edges[anchor-1])>coordinate(edge)){std::swap(edges[anchor],edges[anchor-1]);--anchor;}
            while(anchor+1<edges.size()&&coordinate(edges[anchor+1])<coordinate(edge)){std::swap(edges[anchor],edges[anchor+1]);++anchor;}
        };
        const auto lo=position(source,false),hi=position(source,true);
        if(lo>=edges.size()||hi>=edges.size())continue;
        if(lo&&!ordered(lo)){relocate(false);relocate(true);}
        else if(hi+1!=edges.size()&&!ordered(hi)){relocate(true);relocate(false);}
    }
}
}
