#pragma once
#include "game/content.h"
#include "game/level.h"
#include <functional>
#include <memory>
namespace vyruz {
enum class RouteStatus { Found, Unreachable, SearchLimit, InvalidInput, Pending };
struct NavigationRoute {
    RouteStatus status = RouteStatus::Unreachable;
    std::vector<ContentPosition> points; // excludes start; includes exact goal
    size_t expanded = 0;
};
using RouteBlocked = std::function<bool(ContentPosition,ContentPosition,float)>;
struct NavigationSearchState {
    struct Node { int64_t index; float cost; int64_t parent; bool closed; };
    struct Entry { float estimate,cost; int64_t index; };
    ContentPosition start,goal;
    float radius=0; int width=0,height=0; size_t limit=0; bool initialized=false;
    NavigationRoute result;
    std::vector<Node> nodes;
    std::vector<Entry> open;
};
// Resumable search: each call bounds queue work, including stale entries.
// Dynamic contacts are checked again when the world follows the returned route.
class NavigationSearch {
public:
    NavigationSearch();
    NavigationSearch(const NavigationSearch&);
    NavigationSearch& operator=(const NavigationSearch&);
    NavigationSearch(NavigationSearch&&) noexcept=default;
    NavigationSearch& operator=(NavigationSearch&&) noexcept=default;
    NavigationSearch(const Level&,ContentPosition start,ContentPosition goal,float radius,size_t maximumExpanded=8192);
    NavigationRoute advance(const Level&,const RouteBlocked&,size_t workBudget);
    NavigationSearchState capture() const;
    bool restore(const Level&,const NavigationSearchState&);
private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
};
// Port A* approximation, not the executable's incremental route algorithm.
// Callback returns true when a segment is blocked by a dynamic object.
NavigationRoute findRoute(const Level&, ContentPosition start, ContentPosition goal,
    float radius, const RouteBlocked& dynamicBlocked = {}, size_t maximumExpanded = 8192);
} // namespace vyruz
