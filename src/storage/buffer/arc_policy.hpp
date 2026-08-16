#pragma once

// src/storage/buffer/arc_policy.hpp
//
// Adaptive Replacement Cache (ARC) buffer replacement policy.

#include <list>
#include <span>
#include <unordered_map>

#include "storage/buffer/eviction_policy.hpp"
#include "utils/error.hpp"
#include "utils/primitives.hpp"

namespace edb {

class ArcPolicy final : public EvictionPolicy {
public:
    enum class Location : std::uint8_t {
        T1,
        T2,
        B1,
        B2,
    };

    auto reset(usize capacity) -> VoidResult override;
    auto record_access(u64 page_id, usize frame_index) -> VoidResult override;
    auto record_miss(u64 page_id) -> VoidResult override;
    auto record_load(u64 page_id, usize frame_index) -> VoidResult override;
    auto record_evict(u64 page_id, usize frame_index) -> VoidResult override;
    auto choose_victim(std::span<const EvictionFrameState> frames) -> Result<usize> override;

private:
    auto choose_from_lru(std::list<u64>& pages, std::span<const EvictionFrameState> frames)
        -> Result<usize>;
    auto prune_ghosts() -> void;

    usize capacity{0};
    usize target_recent{0};
    b8 pending_b2_hit{false};
    std::list<u64> t1;
    std::list<u64> t2;
    std::list<u64> b1;
    std::list<u64> b2;
    std::unordered_map<u64, Location> locations;
    std::unordered_map<u64, usize> resident_frames;
};

}  // namespace edb
