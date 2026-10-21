#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <vector>

namespace vyruz {
struct NativeNavGraphEdge {
 uint32_t object=0;bool maximum=false;
 NativeNavGraphEdge()=default;
 NativeNavGraphEdge(size_t id,bool max):object(static_cast<uint32_t>(id)),maximum(max){
  if(id>=UINT32_MAX)throw std::length_error("Graph object index overflow");
 }
};

// Pool indices survive vector reallocation. The ordered list is authoritative;
// object/boundary lookup is derived and never enters the save format.
class NativeNavGraphAxis {
public:
 static constexpr uint32_t none=UINT32_MAX;
 struct Node { NativeNavGraphEdge edge; uint32_t previous=none,next=none; };
private:
 std::vector<Node> nodes_;
 std::vector<uint32_t> free_;
 uint32_t head_=none,tail_=none;size_t count_=0;
 uint32_t insertionHint_=none;
 float insertionLowerBound_=0;
 mutable std::vector<uint32_t> endpoints_;
 mutable bool dirty_=false;
 static size_t key(NativeNavGraphEdge e) {
  if(e.object>(SIZE_MAX-1)/2)throw std::length_error("Graph endpoint index overflow");
  return e.object*2+size_t(e.maximum);
 }
 void rebuildLookup() const {
  endpoints_.clear();
  for(size_t n=head_;n!=none;n=nodes_[n].next){const auto k=key(nodes_[n].edge);if(k>=endpoints_.size())endpoints_.resize(k+1,none);endpoints_[k]=n;}
  dirty_=false;
 }
 void unlink(size_t n) {
  const auto p=nodes_[n].previous,q=nodes_[n].next;
  if(p==none)head_=q;else nodes_[p].next=q;
  if(q==none)tail_=p;else nodes_[q].previous=p;
 }
 void linkBefore(size_t n,size_t before) {
  const auto p=before==none?tail_:nodes_[before].previous;
  nodes_[n].previous=p;nodes_[n].next=before;
  if(p==none)head_=n;else nodes_[p].next=n;
  if(before==none)tail_=n;else nodes_[before].previous=n;
 }
 size_t atRank(size_t rank) const {
  if(rank>=count_)return none;
  size_t n;if(rank<count_/2){n=head_;while(rank--)n=nodes_[n].next;}
  else {n=tail_;for(size_t i=count_-1;i>rank;--i)n=nodes_[n].previous;}
  return n;
 }
public:
 template<bool Const> class Iterator {
  using Axis=std::conditional_t<Const,const NativeNavGraphAxis,NativeNavGraphAxis>;
  Axis* axis_=nullptr;size_t node_=none,rank_=0;
  friend class NativeNavGraphAxis;
  Iterator(Axis* a,size_t n,size_t r):axis_(a),node_(n),rank_(r){}
 public:
  using iterator_category=std::bidirectional_iterator_tag;
  using value_type=NativeNavGraphEdge;using difference_type=std::ptrdiff_t;
  using reference=std::conditional_t<Const,const value_type&,value_type&>;
  using pointer=std::conditional_t<Const,const value_type*,value_type*>;
  Iterator()=default;
  reference operator*()const{if constexpr(!Const){axis_->dirty_=true;axis_->invalidateInsertionHint();}return axis_->nodes_[node_].edge;}
  pointer operator->()const{return &**this;}
  Iterator& operator++(){node_=axis_->nodes_[node_].next;++rank_;return *this;}
  Iterator operator++(int){auto r=*this;++*this;return r;}
  Iterator& operator--(){node_=node_==none?axis_->tail_:axis_->nodes_[node_].previous;--rank_;return *this;}
  Iterator operator--(int){auto r=*this;--*this;return r;}
  friend bool operator==(Iterator a,Iterator b){return a.axis_==b.axis_&&a.node_==b.node_;}
 };
 using iterator=Iterator<false>;using const_iterator=Iterator<true>;
 using value_type=NativeNavGraphEdge;
 NativeNavGraphAxis()=default;
 NativeNavGraphAxis(std::initializer_list<NativeNavGraphEdge> edges){for(auto e:edges)push_back(e);}
 NativeNavGraphAxis& operator=(std::initializer_list<NativeNavGraphEdge> edges){clear();for(auto e:edges)push_back(e);return *this;}
 size_t size()const{return count_;}bool empty()const{return count_==0;}
 // The prefix before the last insertion was inspected and cannot stop a
 // later insertion with an equal/higher coordinate. No sortedness assumed.
 void invalidateInsertionHint(){insertionHint_=none;}
 size_t insertionStart(float coordinate)const{return insertionHint_!=none&&coordinate>=insertionLowerBound_?insertionHint_:head_;}
 void rememberInsertion(size_t node,float coordinate){insertionHint_=static_cast<uint32_t>(node);insertionLowerBound_=coordinate;}
 size_t first()const{return head_;}size_t last()const{return tail_;}
 size_t next(size_t n)const{return nodes_[n].next;}size_t previous(size_t n)const{return nodes_[n].previous;}
 const NativeNavGraphEdge& edge(size_t n)const{return nodes_[n].edge;}
 size_t endpoint(size_t object,bool maximum)const{
  if(dirty_)rebuildLookup();
  if(object>(SIZE_MAX-1)/2)return none;
  const auto k=object*2+size_t(maximum);return k<endpoints_.size()?endpoints_[k]:none;
 }
 size_t insertBefore(size_t before,NativeNavGraphEdge e){
  invalidateInsertionHint();
  if(dirty_)rebuildLookup();
  size_t n;if(free_.empty()){n=nodes_.size();if(n>=none)throw std::length_error("Graph endpoint pool overflow");nodes_.push_back({e});}else{n=free_.back();free_.pop_back();nodes_[n]={e};}
  linkBefore(n,before);++count_;
  const auto k=key(e);if(k>=endpoints_.size())endpoints_.resize(k+1,none);endpoints_[k]=n;return n;
 }
 void eraseNode(size_t n){
  if(n==none)return;
  invalidateInsertionHint();
  if(dirty_)rebuildLookup();
  endpoints_[key(nodes_[n].edge)]=none;unlink(n);free_.push_back(n);--count_;
 }
 void moveBefore(size_t n,size_t before){invalidateInsertionHint();if(n==before||nodes_[n].next==before)return;unlink(n);linkBefore(n,before);}
 void clear(){invalidateInsertionHint();nodes_.clear();free_.clear();endpoints_.clear();head_=tail_=none;count_=0;dirty_=false;}
 void push_back(NativeNavGraphEdge e){insertBefore(none,e);}
 template<class It>void assign(It first,It last){clear();for(;first!=last;++first)push_back(*first);}
 void resize(size_t n){while(count_>n)eraseNode(tail_);while(count_<n)push_back({});dirty_=true;}
 iterator begin(){return {this,head_,0};}iterator end(){return {this,none,count_};}
 const_iterator begin()const{return {this,head_,0};}const_iterator end()const{return {this,none,count_};}
 auto rbegin()const{return std::reverse_iterator(end());}auto rend()const{return std::reverse_iterator(begin());}
 // Rank access is only for fixtures/inspection; runtime traversal uses handles.
 NativeNavGraphEdge& operator[](size_t n){invalidateInsertionHint();dirty_=true;return nodes_[atRank(n)].edge;}
 const NativeNavGraphEdge& operator[](size_t n)const{return nodes_[atRank(n)].edge;}
 NativeNavGraphEdge& front(){return (*this)[0];}const NativeNavGraphEdge& front()const{return (*this)[0];}
 iterator insert(iterator pos,NativeNavGraphEdge e){return {this,insertBefore(pos.node_,e),pos.rank_};}
};
}
