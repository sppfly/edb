// src/storage/buffer/clock_sweep_policy.cpp

#include "storage/buffer/clock_sweep_policy.hpp"

#include <expected>

#include "utils/assert.hpp"

namespace edb {

auto ClockSweepPolicy::reset(usize capacity) -> VoidResult {
    EDB_ASSERT(capacity > usize{0});
    referenced.assign(capacity.value, b8{false});
    clock_hand = usize{0};
    return {};
}

auto ClockSweepPolicy::record_access(u64 /*page_id*/, usize frame_index) -> VoidResult {
    if (frame_index >= usize{referenced.size()}) {
        return std::unexpected(Error::InvalidArgument);
    }
    referenced[frame_index.value] = b8{true};
    return {};
}

auto ClockSweepPolicy::record_miss(u64 /*page_id*/) -> VoidResult {
    return {};
}

auto ClockSweepPolicy::record_load(u64 page_id, usize frame_index) -> VoidResult {
    return record_access(page_id, frame_index);
}

auto ClockSweepPolicy::record_evict(u64 /*page_id*/, usize frame_index) -> VoidResult {
    if (frame_index >= usize{referenced.size()}) {
        return std::unexpected(Error::InvalidArgument);
    }
    referenced[frame_index.value] = b8{false};
    return {};
}

auto ClockSweepPolicy::choose_victim(std::span<const EvictionFrameState> frames) -> Result<usize> {
    if (frames.empty() || referenced.size() != frames.size()) {
        return std::unexpected(Error::InvalidArgument);
    }

    const auto scan_limit = usize{frames.size() * 2U};
    for (usize scans{0}; scans < scan_limit; ++scans) {
        const auto frame_index = clock_hand;
        if (detail::is_evictable(frames, frame_index).value) {
            if (referenced[frame_index.value].value) {
                referenced[frame_index.value] = b8{false};
            } else {
                clock_hand = usize{(clock_hand.value + 1U) % frames.size()};
                return frame_index;
            }
        }
        clock_hand = usize{(clock_hand.value + 1U) % frames.size()};
    }
    return std::unexpected(Error::BufferPoolFull);
}

}  // namespace edb
