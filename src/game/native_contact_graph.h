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
    // A changed endpoint can reach an earlier insertion's coordinate without
    // needing relinking. Its cached prefix is no longer proved strictly lower.
    graph.xEdges.invalidateInsertionHint();graph.yEdges.invalidateInsertionHint();
    graph.objects[source].bounds=bounds;
    graph.xEdges.publishCoordinates(source,bounds.minX,bounds.maxX);
    graph.yEdges.publishCoordinates(source,bounds.minY,bounds.maxY);
    for(bool x:{true,false}){
        auto& edges=x?graph.xEdges:graph.yEdges;
        const auto coordinate=[&](auto edge){auto b=graph.objects[edge.object].bounds;return x?(edge.maximum?b.maxX:b.minX):(edge.maximum?b.maxY:b.minY);};
        const auto ordered=[&](size_t n){const auto p=edges.previous(n),q=edges.next(n);const auto c=coordinate(edges.edge(n));return(p==NativeNavGraphAxis::none||coordinate(edges.edge(p))<=c)&&(q==NativeNavGraphAxis::none||c<=coordinate(edges.edge(q)));};
        const auto relocate=[&](bool maximum){
            const auto n=edges.endpoint(source,maximum),anchor=edges.endpoint(*hint,maximum);if(n==NativeNavGraphAxis::none||anchor==NativeNavGraphAxis::none)return;
            const auto c=coordinate(edges.edge(n));
            edges.moveBefore(n,c>coordinate(edges.edge(anchor))?edges.next(anchor):anchor);
            for(auto prev=edges.previous(n);prev!=NativeNavGraphAxis::none&&coordinate(edges.edge(prev))>c;prev=edges.previous(n))edges.moveBefore(n,prev);
            for(auto next=edges.next(n);next!=NativeNavGraphAxis::none&&coordinate(edges.edge(next))<c;next=edges.next(n))edges.moveBefore(next,n);
        };
        const auto lo=edges.endpoint(source,false),hi=edges.endpoint(source,true);
        if(lo==NativeNavGraphAxis::none||hi==NativeNavGraphAxis::none)continue;
        if(lo!=edges.first()&&!ordered(lo)){relocate(false);relocate(true);}
        else if(hi!=edges.last()&&!ordered(hi)){relocate(true);relocate(false);}
    }
}
}
