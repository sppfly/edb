#pragma once

// src/storage/buffer/eviction_policy.hpp
//
// Pluggable buffer replacement policies. The buffer pool owns pin counts,
// dirty flags, and I/O; eviction policies only rank currently resident frames.

#include <memory>
#include <span>

#include "utils/error.hpp"
#include "utils/primitives.hpp"

namespace edb {

enum class EvictionPolicyKind : std::uint8_t {
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

    virtual auto reset(usize capacity) -> VoidResult = 0;
    virtual auto record_access(u64 page_id, usize frame_index) -> VoidResult = 0;
    virtual auto record_miss(u64 page_id) -> VoidResult = 0;
    virtual auto record_load(u64 page_id, usize frame_index) -> VoidResult = 0;
    virtual auto record_evict(u64 page_id, usize frame_index) -> VoidResult = 0;
    virtual auto choose_victim(std::span<const EvictionFrameState> frames) -> Result<usize> = 0;
};

// Shared helper used by concrete policies to rank resident frames.
namespace detail {

inline auto is_evictable(std::span<const EvictionFrameState> frames, usize frame_index) -> b8 {
    if (frame_index >= usize{frames.size()}) {
        return b8{false};
    }
    const auto& frame = frames[frame_index.value];
    return b8{frame.valid.value && !frame.pinned.value};
}

}  // namespace detail

auto make_eviction_policy(const EvictionPolicyConfig& config) -> std::unique_ptr<EvictionPolicy>;

}  // namespace edb