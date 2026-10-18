#pragma once
#include "assets/content_text.h"
#include <memory>
#include <utility>

namespace vyruz {
// Value semantics for mostly immutable authored properties. Snapshots and spawned
// instances share definitions; any mutable access first makes an independent copy.
// No implicit mutable-vector conversion: ordinary property lookup must stay cheap.
class SharedProperties {
    std::shared_ptr<std::vector<TextNode>> data_;
public:
    SharedProperties()=default;
    SharedProperties(std::vector<TextNode> nodes) { if(!nodes.empty())data_=std::make_shared<std::vector<TextNode>>(std::move(nodes)); }
    SharedProperties(std::initializer_list<TextNode> nodes):SharedProperties(std::vector<TextNode>(nodes)){}
    const std::vector<TextNode>& read() const {
        static const std::vector<TextNode> empty;
        return data_?*data_:empty;
    }
    std::vector<TextNode>& write() {
        if(!data_)data_=std::make_shared<std::vector<TextNode>>();
        else if(data_.use_count()!=1)data_=std::make_shared<std::vector<TextNode>>(*data_);
        return *data_;
    }
    operator const std::vector<TextNode>&() const { return read(); }
    size_t size() const {return read().size();}
    bool empty() const {return read().empty();}
    auto begin() const {return read().begin();}
    auto end() const {return read().end();}
    auto begin() {return write().begin();}
    auto end() {return write().end();}
    const TextNode& operator[](size_t i) const {return read()[i];}
    TextNode& operator[](size_t i) {return write()[i];}
    const TextNode& back() const {return read().back();}
    TextNode& back() {return write().back();}
    const TextNode& front() const {return read().front();}
    TextNode& front() {return write().front();}
    const TextNode& at(size_t i) const {return read().at(i);}
    TextNode& at(size_t i) {return write().at(i);}
    auto rbegin() const {return read().rbegin();}
    auto rend() const {return read().rend();}
    auto rbegin() {return write().rbegin();}
    auto rend() {return write().rend();}
    using const_iterator=std::vector<TextNode>::const_iterator;
    auto insert(const_iterator at,std::initializer_list<TextNode> nodes) {
        const auto offset=empty()?0:at-read().begin();auto& v=write();return v.insert(v.begin()+offset,nodes);
    }
    template<class Iterator> auto insert(const_iterator at,Iterator first,Iterator last) {
        const auto offset=empty()?0:at-read().begin();auto& v=write();return v.insert(v.begin()+offset,first,last);
    }
    auto erase(const_iterator first,const_iterator last) {
        const auto start=first-read().begin(),finish=last-read().begin();auto& v=write();return v.erase(v.begin()+start,v.begin()+finish);
    }
    auto erase(const_iterator at) {return erase(at,at+1);}
    void clear() {data_.reset();}
    void push_back(TextNode node) {write().push_back(std::move(node));}
    template<class... Args> TextNode& emplace_back(Args&&... args) {return write().emplace_back(std::forward<Args>(args)...);}
};
}
