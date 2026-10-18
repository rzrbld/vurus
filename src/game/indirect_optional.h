#pragma once
#include <memory>
#include <utility>

namespace vyruz {
// Optional large state with value semantics. Ordinary effects must not carry
// inline storage for both enemy AI implementations. Copies own independent
// state, including snapshots; only moves transfer ownership.
template<class T> class IndirectOptional {
    std::unique_ptr<T> value_;
public:
    using value_type=T;
    IndirectOptional()=default;
    IndirectOptional(const IndirectOptional& other)
        :value_(other?std::make_unique<T>(*other):nullptr) {}
    IndirectOptional(IndirectOptional&&) noexcept=default;
    IndirectOptional& operator=(const IndirectOptional& other) {
        if(this!=&other){IndirectOptional copy(other);value_.swap(copy.value_);}
        return *this;
    }
    IndirectOptional& operator=(IndirectOptional&&) noexcept=default;
    IndirectOptional& operator=(T value) {emplace(std::move(value));return *this;}
    explicit operator bool() const noexcept {return bool(value_);}
    bool has_value() const noexcept {return bool(value_);}
    T& operator*() noexcept {return *value_;}
    const T& operator*() const noexcept {return *value_;}
    T* operator->() noexcept {return value_.get();}
    const T* operator->() const noexcept {return value_.get();}
    void reset() noexcept {value_.reset();}
    template<class... Args> T& emplace(Args&&... args) {
        auto next=std::make_unique<T>(std::forward<Args>(args)...);
        value_=std::move(next);return *value_;
    }
};
}
