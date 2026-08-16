// src/storage/buffer/lru_k_policy.cpp

#include "storage/buffer/lru_k_policy.hpp"

#include <expected>

#include "utils/assert.hpp"

namespace edb {

LruKPolicy::LruKPolicy(usize history_count) : k_history{history_count} {}

auto LruKPolicy::reset(usize capacity) -> VoidResult {
    EDB_ASSERT(capacity > usize{0});
    (void)capacity;
    tick = u64{0};
    histories.clear();
    return {};
}

auto LruKPolicy::record_access(u64 page_id, usize /*frame_index*/) -> VoidResult {
    ++tick;
    auto& history = histories[page_id];
    history.push_back(tick);
    while (history.size() > k_history.value) {
        history.pop_front();
    }
    return {};
}

auto LruKPolicy::record_miss(u64 /*page_id*/) -> VoidResult {
    return {};
}

auto LruKPolicy::record_load(u64 page_id, usize frame_index) -> VoidResult {
    return record_access(page_id, frame_index);
}

auto LruKPolicy::record_evict(u64 /*page_id*/, usize /*frame_index*/) -> VoidResult {
    return {};
}

auto LruKPolicy::choose_victim(std::span<const EvictionFrameState> frames) -> Result<usize> {
    auto found = b8{false};
    auto victim = usize{0};
    auto victim_has_full_history = b8{true};
    auto victim_score = u64{0};

    for (usize frame_index{0}; frame_index < usize{frames.size()}; ++frame_index) {
        const auto& frame = frames[frame_index.value];
        if (!frame.valid.value || frame.pinned.value) {
            continue;
        }

        const auto iter = histories.find(frame.page_id);
        if (iter == histories.end() || iter->second.empty()) {
            return frame_index;
        }

        const auto has_full_history = b8{iter->second.size() >= k_history.value};
        const auto score = iter->second.front();
        if (!found.value) {
            found = b8{true};
            victim = frame_index;
            victim_has_full_history = has_full_history;
            victim_score = score;
            continue;
        }

        if (!has_full_history.value && victim_has_full_history.value) {
            victim = frame_index;
            victim_has_full_history = has_full_history;
            victim_score = score;
            continue;
        }
        if (has_full_history.value == victim_has_full_history.value && score < victim_score) {
            victim = frame_index;
            victim_score = score;
        }
    }

    if (!found.value) {
        return std::unexpected(Error::BufferPoolFull);
    }
    return victim;
}

}  // namespace edb
