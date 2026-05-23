#pragma once

// src/storage/buffer/eviction_policy.hpp
//
// Pluggable buffer replacement policies. The buffer pool owns pin counts,
// dirty flags, and I/O; eviction policies only rank currently resident frames.

#include <deque>
#include <list>
#include <memory>
#include <span>
#include <unordered_map>
#include <vector>

#include "utils/contracts.hpp"
#include "utils/error.hpp"
#include "utils/primitives.hpp"

namespace edb {

enum class EvictionPolicyKind: std::uint8_t {
    ClockSweep,
    LruK,
    Arc,
};

struct EvictionPolicyConfig {
    EvictionPolicyKind kind{EvictionPolicyKind::ClockSweep};
    usize lru_k_history{2};
};

struct EvictionFrameState {
    u64 page_id{0};
    b8 valid{false};
    b8 pinned{false};
};

class EvictionPolicy {
   public:
    EvictionPolicy() = default;
    EvictionPolicy(const EvictionPolicy&) = delete;
    EvictionPolicy& operator=(const EvictionPolicy&) = delete;
    EvictionPolicy(EvictionPolicy&&) = delete;
    EvictionPolicy& operator=(EvictionPolicy&&) = delete;
    virtual ~EvictionPolicy() = default;

    auto reset(usize capacity) -> VoidResult EDB_PRE(capacity > usize{0}) {
        return reset_impl(capacity);
    }
    auto record_access(u64 page_id, usize frame_index) -> VoidResult {
        return record_access_impl(page_id, frame_index);
    }
    auto record_miss(u64 page_id) -> VoidResult { return record_miss_impl(page_id); }
    auto record_load(u64 page_id, usize frame_index) -> VoidResult {
        return record_load_impl(page_id, frame_index);
    }
    auto record_evict(u64 page_id, usize frame_index) -> VoidResult {
        return record_evict_impl(page_id, frame_index);
    }
    auto choose_victim(std::span<const EvictionFrameState> frames) -> Result<usize> {
        return choose_victim_impl(frames);
    }

   private:
    virtual auto reset_impl(usize capacity) -> VoidResult = 0;
    virtual auto record_access_impl(u64 page_id, usize frame_index) -> VoidResult = 0;
    virtual auto record_miss_impl(u64 page_id) -> VoidResult = 0;
    virtual auto record_load_impl(u64 page_id, usize frame_index) -> VoidResult = 0;
    virtual auto record_evict_impl(u64 page_id, usize frame_index) -> VoidResult = 0;
    virtual auto choose_victim_impl(std::span<const EvictionFrameState> frames)
        -> Result<usize> = 0;
};

class ClockSweepPolicy final : public EvictionPolicy {
   private:
    auto reset_impl(usize capacity) -> VoidResult override;
    auto record_access_impl(u64 page_id, usize frame_index) -> VoidResult override;
    auto record_miss_impl(u64 page_id) -> VoidResult override;
    auto record_load_impl(u64 page_id, usize frame_index) -> VoidResult override;
    auto record_evict_impl(u64 page_id, usize frame_index) -> VoidResult override;
    auto choose_victim_impl(std::span<const EvictionFrameState> frames)
        -> Result<usize> override;

    std::vector<b8> referenced;
    usize clock_hand{0};
};

class LruKPolicy final : public EvictionPolicy {
   public:
    explicit LruKPolicy(usize history_count);

   private:
    auto reset_impl(usize capacity) -> VoidResult override;
    auto record_access_impl(u64 page_id, usize frame_index) -> VoidResult override;
    auto record_miss_impl(u64 page_id) -> VoidResult override;
    auto record_load_impl(u64 page_id, usize frame_index) -> VoidResult override;
    auto record_evict_impl(u64 page_id, usize frame_index) -> VoidResult override;
    auto choose_victim_impl(std::span<const EvictionFrameState> frames)
        -> Result<usize> override;

    usize k_history{2};
    u64 tick{0};
    std::unordered_map<u64, std::deque<u64>> histories;
};

class ArcPolicy final : public EvictionPolicy {
   public:
    enum class Location: std::uint8_t {
        T1,
        T2,
        B1,
        B2,
    };

   private:
    auto reset_impl(usize capacity) -> VoidResult override;
    auto record_access_impl(u64 page_id, usize frame_index) -> VoidResult override;
    auto record_miss_impl(u64 page_id) -> VoidResult override;
    auto record_load_impl(u64 page_id, usize frame_index) -> VoidResult override;
    auto record_evict_impl(u64 page_id, usize frame_index) -> VoidResult override;
    auto choose_victim_impl(std::span<const EvictionFrameState> frames)
        -> Result<usize> override;

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

auto make_eviction_policy(const EvictionPolicyConfig& config)
    -> std::unique_ptr<EvictionPolicy>;

}  // namespace edb