#pragma once
#ifndef VURUS_ACTOR_SYNC_PROFILE
#define VURUS_ACTOR_SYNC_PROFILE 0
#endif
#if VURUS_ACTOR_SYNC_PROFILE
#include <cstdint>
#include <cstddef>
#include <ctime>
namespace vyruz::actorSyncProfile {
inline bool enabled=false;
struct Counts { uint64_t calls=0,visits=0,skips=0,advanceVisits=0,cpuNs=0; };
inline Counts counts;
inline uint64_t clockNs(){timespec ts{};clock_gettime(CLOCK_THREAD_CPUTIME_ID,&ts);return uint64_t(ts.tv_sec)*1000000000ull+uint64_t(ts.tv_nsec);}
struct Scope {uint64_t begin=0; explicit Scope(size_t n){if(enabled){begin=clockNs();++counts.calls;counts.visits+=n;}}~Scope(){if(begin)counts.cpuNs+=clockNs()-begin;}};
}
#endif
