#pragma once
#include <cstddef>
#include <cstdint>
#include <list>
#include <unordered_map>

namespace vyruz {
// Bounded GPU cache. Slots belong to stable frame arrays; reset before their
// owners are destroyed. Eviction only drops a texture, never its source image.
class TextureResidency {
    struct Entry { unsigned* slot; size_t bytes; uint64_t draw; };
    std::list<Entry> recent_;
    std::unordered_map<unsigned,decltype(recent_)::iterator> index_;
    size_t bytes_=0;
    uint64_t draw_=0;
public:
    TextureResidency()=default;
    TextureResidency(const TextureResidency&)=delete;
    TextureResidency& operator=(const TextureResidency&)=delete;
    TextureResidency(TextureResidency&&)=default;
    TextureResidency& operator=(TextureResidency&&)=default;
    void beginDraw(){++draw_;}
    void touch(unsigned& slot,size_t bytes){
        if(!slot)return;
        const auto found=index_.find(slot);
        if(found!=index_.end()){
            bytes_-=found->second->bytes;
            found->second->bytes=bytes;bytes_+=bytes;
            found->second->draw=draw_;
            recent_.splice(recent_.begin(),recent_,found->second);
        }else{
            recent_.push_front({&slot,bytes,draw_});index_[slot]=recent_.begin();bytes_+=bytes;
        }
    }
    template<class Delete> void trim(size_t byteLimit,size_t countLimit,Delete remove){
        trimWithOverhead(byteLimit,countLimit,[]{return size_t(0);},remove);
    }
    // Atlas holes still occupy GPU storage. Re-evaluate after each eviction:
    // releasing a frame can increase slack or release its whole page.
    template<class Overhead,class Delete> void trimWithOverhead(size_t byteLimit,size_t countLimit,Overhead overhead,Delete remove){
        while(!recent_.empty()&&(bytes_+overhead()>byteLimit||recent_.size()>countLimit)){
            auto& old=recent_.back();
            // An unusually large visible working set is allowed to exceed the
            // budget: never churn textures still needed by this draw.
            if(old.draw==draw_)break;
            const auto texture=*old.slot;remove(texture);*old.slot=0;
            bytes_-=old.bytes;index_.erase(texture);recent_.pop_back();
        }
    }
    size_t bytes()const{return bytes_;}
    size_t count()const{return recent_.size();}
    void clear(){index_.clear();recent_.clear();bytes_=0;draw_=0;}
};
}
