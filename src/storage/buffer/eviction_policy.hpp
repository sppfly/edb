#pragma once

// src/storage/buffer/eviction_policy.hpp
//
// Pluggable buffer replacement policies. The buffer pool owns pin counts,
// dirty flags, and I/O; eviction policies only rank currently resident frames.

#include <cstddef>
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

enum class EdbEvictionPolicyKind {
    ClockSweep,
    LruK,
    Arc,
};

struct EdbEvictionPolicyConfig {
    EdbEvictionPolicyKind kind{EdbEvictionPolicyKind::ClockSweep};
    usize lru_k_history{2};
};

struct EdbEvictionFrameState {
    u64 page_id{0};
    b8 valid{false};
    b8 pinned{false};
};

class EdbEvictionPolicy {
   public:
    EdbEvictionPolicy() = default;
    EdbEvictionPolicy(const EdbEvictionPolicy&) = delete;
    EdbEvictionPolicy& operator=(const EdbEvictionPolicy&) = delete;
    EdbEvictionPolicy(EdbEvictionPolicy&&) = delete;
    EdbEvictionPolicy& operator=(EdbEvictionPolicy&&) = delete;
    virtual ~EdbEvictionPolicy() = default;

    auto reset(usize capacity) -> EdbStatus EDB_PRE(capacity > usize{0}) {
        return reset_impl(capacity);
    }
    auto record_access(u64 page_id, usize frame_index) -> EdbStatus {
        return record_access_impl(page_id, frame_index);
    }
    auto record_miss(u64 page_id) -> EdbStatus { return record_miss_impl(page_id); }
    auto record_load(u64 page_id, usize frame_index) -> EdbStatus {
        return record_load_impl(page_id, frame_index);
    }
    auto record_evict(u64 page_id, usize frame_index) -> EdbStatus {
        return record_evict_impl(page_id, frame_index);
    }
    auto choose_victim(std::span<const EdbEvictionFrameState> frames) -> EdbResult<usize> {
        return choose_victim_impl(frames);
    }

   private:
    virtual auto reset_impl(usize capacity) -> EdbStatus = 0;
    virtual auto record_access_impl(u64 page_id, usize frame_index) -> EdbStatus = 0;
    virtual auto record_miss_impl(u64 page_id) -> EdbStatus = 0;
    virtual auto record_load_impl(u64 page_id, usize frame_index) -> EdbStatus = 0;
    virtual auto record_evict_impl(u64 page_id, usize frame_index) -> EdbStatus = 0;
    virtual auto choose_victim_impl(std::span<const EdbEvictionFrameState> frames)
        -> EdbResult<usize> = 0;
};

class EdbClockSweepPolicy final : public EdbEvictionPolicy {
   private:
    auto reset_impl(usize capacity) -> EdbStatus override;
    auto record_access_impl(u64 page_id, usize frame_index) -> EdbStatus override;
    auto record_miss_impl(u64 page_id) -> EdbStatus override;
    auto record_load_impl(u64 page_id, usize frame_index) -> EdbStatus override;
    auto record_evict_impl(u64 page_id, usize frame_index) -> EdbStatus override;
    auto choose_victim_impl(std::span<const EdbEvictionFrameState> frames)
        -> EdbResult<usize> override;

    std::vector<b8> referenced;
    usize clock_hand{0};
};

class EdbLruKPolicy final : public EdbEvictionPolicy {
   public:
    explicit EdbLruKPolicy(usize history_count);

   private:
    auto reset_impl(usize capacity) -> EdbStatus override;
    auto record_access_impl(u64 page_id, usize frame_index) -> EdbStatus override;
    auto record_miss_impl(u64 page_id) -> EdbStatus override;
    auto record_load_impl(u64 page_id, usize frame_index) -> EdbStatus override;
    auto record_evict_impl(u64 page_id, usize frame_index) -> EdbStatus override;
    auto choose_victim_impl(std::span<const EdbEvictionFrameState> frames)
        -> EdbResult<usize> override;

    usize k_history{2};
    u64 tick{0};
    std::unordered_map<u64, std::deque<u64>> histories;
};

class EdbArcPolicy final : public EdbEvictionPolicy {
   public:
    enum class Location {
        T1,
        T2,
        B1,
        B2,
    };

   private:
    auto reset_impl(usize capacity) -> EdbStatus override;
    auto record_access_impl(u64 page_id, usize frame_index) -> EdbStatus override;
    auto record_miss_impl(u64 page_id) -> EdbStatus override;
    auto record_load_impl(u64 page_id, usize frame_index) -> EdbStatus override;
    auto record_evict_impl(u64 page_id, usize frame_index) -> EdbStatus override;
    auto choose_victim_impl(std::span<const EdbEvictionFrameState> frames)
        -> EdbResult<usize> override;

    auto choose_from_lru(std::list<u64>& pages, std::span<const EdbEvictionFrameState> frames)
        -> EdbResult<usize>;
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

auto make_eviction_policy(const EdbEvictionPolicyConfig& config)
    -> std::unique_ptr<EdbEvictionPolicy>;

}  // namespace edb