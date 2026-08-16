#pragma once

// src/storage/buffer/clock_sweep_policy.hpp
//
// Clock-sweep (second-chance) buffer replacement policy.

#include <span>
#include <vector>

#include "storage/buffer/eviction_policy.hpp"
#include "utils/error.hpp"
#include "utils/primitives.hpp"

namespace edb {

class ClockSweepPolicy final : public EvictionPolicy {
public:
    auto reset(usize capacity) -> VoidResult override;
    auto record_access(u64 page_id, usize frame_index) -> VoidResult override;
    auto record_miss(u64 page_id) -> VoidResult override;
    auto record_load(u64 page_id, usize frame_index) -> VoidResult override;
    auto record_evict(u64 page_id, usize frame_index) -> VoidResult override;
    auto choose_victim(std::span<const EvictionFrameState> frames) -> Result<usize> override;

private:
    std::vector<b8> referenced;
    usize clock_hand{0};
};

}  // namespace edb
