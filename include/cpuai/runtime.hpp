#pragma once
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory_resource>
#include <optional>
#include <stop_token>
#include <stdexcept>

namespace cpuai {
using SteadyClock = std::chrono::steady_clock;
enum class StopReason { complete, work_slice, cancelled, deadline, candidate_budget, unique_limit, memory_limit };
inline const char* reasonName(StopReason r) {
    switch(r) {
    case StopReason::complete:return "complete";
    case StopReason::work_slice:return "work_slice";
    case StopReason::cancelled:return "cancelled";
    case StopReason::deadline:return "deadline";
    case StopReason::candidate_budget:return "candidate_budget";
    case StopReason::unique_limit:return "unique_limit";
    case StopReason::memory_limit:return "memory_limit";
    }
    return "unknown";
}
struct SearchOptions {
    std::size_t budget=50000;
    int maxDepth=2;
    std::size_t maxUnique=4096, maxHypotheses=64;
    std::size_t maxStorageBytes=4*1024*1024;
    bool operator==(const SearchOptions&) const = default;
};
inline void validateOptions(const SearchOptions& o) {
    if(o.budget>1000000 || o.maxDepth<0 || o.maxDepth>4 || o.maxUnique<8
        || o.maxUnique>50000 || o.maxHypotheses==0 || o.maxHypotheses>256
        || o.maxStorageBytes>128*1024*1024)
        throw std::runtime_error("invalid search limits");
}
struct SearchControl {
    std::size_t maxCombinations=std::numeric_limits<std::size_t>::max();
    std::stop_token cancellation;
    std::optional<SteadyClock::time_point> deadline;
    std::function<SteadyClock::time_point()> now=[] {return SteadyClock::now();};
};
inline std::optional<StopReason> interrupt(const SearchControl& c) {
    if(c.cancellation.stop_requested()) return StopReason::cancelled;
    if(c.deadline && c.now()>=*c.deadline) return StopReason::deadline;
    return std::nullopt;
}
struct MemoryLimit final:std::bad_alloc {};
// Limits requested bytes for DAG buffers and index nodes, not process RSS or allocator metadata.
class BudgetResource final:public std::pmr::memory_resource {
    std::size_t limit_, used_=0, peak_=0;
    void* do_allocate(std::size_t n,std::size_t alignment) override {
        if(n>limit_-used_) throw MemoryLimit{};
        void* p=std::pmr::new_delete_resource()->allocate(n,alignment);
        used_+=n; peak_=std::max(peak_,used_); return p;
    }
    void do_deallocate(void* p,std::size_t n,std::size_t alignment) override {
        std::pmr::new_delete_resource()->deallocate(p,n,alignment); used_-=n;
    }
    bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override {return this==&other;}
public:
    explicit BudgetResource(std::size_t limit):limit_(limit){}
    std::size_t used()const{return used_;}
    std::size_t peak()const{return peak_;}
};
struct QueryMetrics {
    std::size_t newCombinations=0, exampleChecks=0, storageBytes=0, peakStorageBytes=0, dagEdges=0;
    bool cacheHit=false, incremental=false;
    std::uint64_t elapsedMicros=0;
};
}
