#pragma once

// src/storage/buffer/lru_k_policy.hpp
//
// LRU-K buffer replacement policy: tracks the last K access times per page.

#include <deque>
#include <span>
#include <unordered_map>

#include "storage/buffer/eviction_policy.hpp"
#include "utils/error.hpp"
#include "utils/primitives.hpp"

namespace edb {

class LruKPolicy final : public EvictionPolicy {
public:
    explicit LruKPolicy(usize history_count);

    auto reset(usize capacity) -> VoidResult override;
    auto record_access(u64 page_id, usize frame_index) -> VoidResult override;
    auto record_miss(u64 page_id) -> VoidResult override;
    auto record_load(u64 page_id, usize frame_index) -> VoidResult override;
    auto record_evict(u64 page_id, usize frame_index) -> VoidResult override;
    auto choose_victim(std::span<const EvictionFrameState> frames) -> Result<usize> override;

private:
    usize k_history{2};
    u64 tick{0};
    std::unordered_map<u64, std::deque<u64>> histories;
};

}  // namespace edb
