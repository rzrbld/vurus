#include "game/actor_pool_native.h"
#include <cassert>
#include <cstdint>
#include <limits>

int main() {
    using namespace vyruz;
    NativeActorPool pool;
    const auto a=pool.allocate(),b=pool.allocate(),c=pool.allocate();
    assert(a==(NativeActorPoolAllocation{0,1}));
    assert(b==(NativeActorPoolAllocation{1,2}));
    assert(c==(NativeActorPoolAllocation{2,3}));
    assert(pool.release(b.slot));
    assert(pool.catalog()==(std::vector<std::size_t>{0,2,1}));
    assert(pool.nextGeneration()==4 && pool.liveCount()==2 && !pool.release(b.slot));
    assert(pool.allocate()==(NativeActorPoolAllocation{1,4}));
    auto staged=pool;
    assert(staged.allocate()==(NativeActorPoolAllocation{3,5}));
    assert(pool.nextGeneration()==5); // failed construction rollback is caller-owned.
    pool=std::move(staged);
    assert(pool.allocate()==(NativeActorPoolAllocation{4,6}));
    NativeActorPool restored;
    assert(restored.restore(pool.snapshot()));
    assert(restored.allocate()==pool.allocate());
    auto corrupt=pool.snapshot();corrupt.catalog[1]=corrupt.catalog[0];
    const auto before=restored.snapshot();
    assert(!restored.restore(corrupt) && restored.snapshot()==before);
    NativeActorPool wrap(std::numeric_limits<std::uint32_t>::max());
    assert(wrap.allocate().generation==std::numeric_limits<std::uint32_t>::max());
    const auto zero=wrap.allocate();assert(zero.generation==0 && wrap.liveCount()==2);
    assert(wrap.release(zero.slot) && wrap.allocate().generation==1);
}
