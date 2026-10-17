#pragma once
#include <bit>
#include <cstdint>

namespace vyruz {
// Original428db0 reset and4540c0 inclusive integer draw. This value object
// represents the world259c context without mutating an outer/global stream.
// Port MT consumers remain separate until their original call order is traced.
struct NativeRandomState {
    std::uint32_t seed=0xfeabcefeu,counter=0xabcdefdau;
    std::uint32_t raw() {
        const std::uint32_t product=seed*counter++;
        seed=std::rotl(seed,static_cast<int>(product&31u));
        return product;
    }
    int inclusive(int low,int high) {
        const auto product=raw();
        const auto width=static_cast<std::uint64_t>(static_cast<std::int64_t>(high)-low+1);
        return static_cast<int>(static_cast<std::int64_t>(low)+((product*width)>>32));
    }
};
}
