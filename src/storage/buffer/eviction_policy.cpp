// src/storage/buffer/eviction_policy.cpp

#include "storage/buffer/eviction_policy.hpp"

#include <algorithm>
#include <deque>
#include <expected>
#include <list>
#include <unordered_map>
#include <vector>

namespace edb {

namespace {

auto is_evictable(std::span<const EdbEvictionFrameState> frames, usize frame_index) -> b8 {
    if (frame_index >= usize{frames.size()}) {
        return b8{false};
    }
    const auto& frame = frames[frame_index.value];
    return b8{frame.valid.value && !frame.pinned.value};
}

auto erase_page(std::list<u64>& pages, u64 page_id) -> void {
    auto iter = std::ranges::find(pages, page_id);
    if (iter != pages.end()) {
        pages.erase(iter);
    }
}

auto push_front_unique(std::list<u64>& pages, u64 page_id) -> void {
    erase_page(pages, page_id);
    pages.push_front(page_id);
}

auto remove_lru_ghost(std::list<u64>& ghosts,
                      std::unordered_map<u64, EdbArcPolicy::Location>& locations) -> void {
    if (ghosts.empty()) {
        return;
    }
    const auto page_id = ghosts.back();
    ghosts.pop_back();
    locations.erase(page_id);
}

}  // namespace

auto EdbClockSweepPolicy::reset_impl(usize capacity) -> EdbStatus {
    referenced.assign(capacity.value, b8{false});
    clock_hand = usize{0};
    return {};
}

auto EdbClockSweepPolicy::record_access_impl(u64 /*page_id*/, usize frame_index) -> EdbStatus {
    if (frame_index >= usize{referenced.size()}) {
        return std::unexpected(EdbError::InvalidArgument);
    }
    referenced[frame_index.value] = b8{true};
    return {};
}

auto EdbClockSweepPolicy::record_miss_impl(u64 /*page_id*/) -> EdbStatus {
    return {};
}

auto EdbClockSweepPolicy::record_load_impl(u64 page_id, usize frame_index) -> EdbStatus {
    return record_access_impl(page_id, frame_index);
}

auto EdbClockSweepPolicy::record_evict_impl(u64 /*page_id*/, usize frame_index) -> EdbStatus {
    if (frame_index >= usize{referenced.size()}) {
        return std::unexpected(EdbError::InvalidArgument);
    }
    referenced[frame_index.value] = b8{false};
    return {};
}

auto EdbClockSweepPolicy::choose_victim_impl(std::span<const EdbEvictionFrameState> frames)
    -> EdbResult<usize> {
    if (frames.empty() || referenced.size() != frames.size()) {
        return std::unexpected(EdbError::InvalidArgument);
    }

    const auto scan_limit = usize{frames.size() * 2U};
    for (usize scans{0}; scans < scan_limit; ++scans) {
        const auto frame_index = clock_hand;
        if (is_evictable(frames, frame_index).value) {
            if (referenced[frame_index.value].value) {
                referenced[frame_index.value] = b8{false};
            } else {
                clock_hand = usize{(clock_hand.value + 1U) % frames.size()};
                return frame_index;
            }
        }
        clock_hand = usize{(clock_hand.value + 1U) % frames.size()};
    }
    return std::unexpected(EdbError::BufferPoolFull);
}

EdbLruKPolicy::EdbLruKPolicy(usize history_count) : k_history{history_count} {}

auto EdbLruKPolicy::reset_impl(usize /*capacity*/) -> EdbStatus {
    tick = u64{0};
    histories.clear();
    return {};
}

auto EdbLruKPolicy::record_access_impl(u64 page_id, usize /*frame_index*/) -> EdbStatus {
    ++tick;
    auto& history = histories[page_id];
    history.push_back(tick);
    while (history.size() > k_history.value) {
        history.pop_front();
    }
    return {};
}

auto EdbLruKPolicy::record_miss_impl(u64 /*page_id*/) -> EdbStatus {
    return {};
}

auto EdbLruKPolicy::record_load_impl(u64 page_id, usize frame_index) -> EdbStatus {
    return record_access_impl(page_id, frame_index);
}

auto EdbLruKPolicy::record_evict_impl(u64 /*page_id*/, usize /*frame_index*/) -> EdbStatus {
    return {};
}

auto EdbLruKPolicy::choose_victim_impl(std::span<const EdbEvictionFrameState> frames)
    -> EdbResult<usize> {
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
        return std::unexpected(EdbError::BufferPoolFull);
    }
    return victim;
}

auto EdbArcPolicy::reset_impl(usize new_capacity) -> EdbStatus {
    capacity = new_capacity;
    target_recent = usize{0};
    pending_b2_hit = b8{false};
    t1.clear();
    t2.clear();
    b1.clear();
    b2.clear();
    locations.clear();
    resident_frames.clear();
    return {};
}

auto EdbArcPolicy::record_access_impl(u64 page_id, usize frame_index) -> EdbStatus {
    resident_frames[page_id] = frame_index;
    const auto iter = locations.find(page_id);
    if (iter == locations.end()) {
        locations[page_id] = Location::T1;
        push_front_unique(t1, page_id);
        return {};
    }

    if (iter->second == Location::T1) {
        erase_page(t1, page_id);
        push_front_unique(t2, page_id);
        iter->second = Location::T2;
        return {};
    }
    if (iter->second == Location::T2) {
        push_front_unique(t2, page_id);
        return {};
    }
    return {};
}

auto EdbArcPolicy::record_miss_impl(u64 page_id) -> EdbStatus {
    pending_b2_hit = b8{false};
    const auto iter = locations.find(page_id);
    if (iter == locations.end()) {
        return {};
    }

    if (iter->second == Location::B1) {
        const auto delta =
            std::max(std::size_t{1}, b2.size() / std::max(std::size_t{1}, b1.size()));
        target_recent = usize{std::min(capacity.value, target_recent.value + delta)};
        return {};
    }
    if (iter->second == Location::B2) {
        const auto delta =
            std::max(std::size_t{1}, b1.size() / std::max(std::size_t{1}, b2.size()));
        target_recent = usize{target_recent.value > delta ? target_recent.value - delta : 0U};
        pending_b2_hit = b8{true};
    }
    return {};
}

auto EdbArcPolicy::record_load_impl(u64 page_id, usize frame_index) -> EdbStatus {
    resident_frames[page_id] = frame_index;
    const auto iter = locations.find(page_id);
    if (iter != locations.end() && iter->second == Location::B1) {
        erase_page(b1, page_id);
        push_front_unique(t2, page_id);
        iter->second = Location::T2;
        prune_ghosts();
        return {};
    }
    if (iter != locations.end() && iter->second == Location::B2) {
        erase_page(b2, page_id);
        push_front_unique(t2, page_id);
        iter->second = Location::T2;
        prune_ghosts();
        return {};
    }

    locations[page_id] = Location::T1;
    push_front_unique(t1, page_id);
    prune_ghosts();
    return {};
}

auto EdbArcPolicy::record_evict_impl(u64 page_id, usize /*frame_index*/) -> EdbStatus {
    resident_frames.erase(page_id);
    const auto iter = locations.find(page_id);
    if (iter == locations.end()) {
        return {};
    }

    if (iter->second == Location::T1) {
        erase_page(t1, page_id);
        push_front_unique(b1, page_id);
        iter->second = Location::B1;
    } else if (iter->second == Location::T2) {
        erase_page(t2, page_id);
        push_front_unique(b2, page_id);
        iter->second = Location::B2;
    }
    prune_ghosts();
    return {};
}

auto EdbArcPolicy::choose_victim_impl(std::span<const EdbEvictionFrameState> frames)
    -> EdbResult<usize> {
    if (t1.size() > target_recent.value ||
        (pending_b2_hit.value && t1.size() == target_recent.value)) {
        auto recent = choose_from_lru(t1, frames);
        if (recent) {
            return recent;
        }
    }

    auto frequent = choose_from_lru(t2, frames);
    if (frequent) {
        return frequent;
    }
    auto recent = choose_from_lru(t1, frames);
    if (recent) {
        return recent;
    }
    return std::unexpected(EdbError::BufferPoolFull);
}

auto EdbArcPolicy::choose_from_lru(std::list<u64>& pages,
                                   std::span<const EdbEvictionFrameState> frames)
    -> EdbResult<usize> {
    for (auto iter = pages.rbegin(); iter != pages.rend(); ++iter) {
        const auto frame = resident_frames.find(*iter);
        if (frame != resident_frames.end() && is_evictable(frames, frame->second).value) {
            return frame->second;
        }
    }
    return std::unexpected(EdbError::NotFound);
}

auto EdbArcPolicy::prune_ghosts() -> void {
    while (b1.size() > capacity.value) {
        remove_lru_ghost(b1, locations);
    }
    while (b2.size() > capacity.value) {
        remove_lru_ghost(b2, locations);
    }
    while ((b1.size() + b2.size()) > capacity.value) {
        if (b1.size() >= b2.size()) {
            remove_lru_ghost(b1, locations);
        } else {
            remove_lru_ghost(b2, locations);
        }
    }
}

auto make_eviction_policy(const EdbEvictionPolicyConfig& config)
    -> std::unique_ptr<EdbEvictionPolicy> {
    switch (config.kind) {
        case EdbEvictionPolicyKind::ClockSweep:
            return std::make_unique<EdbClockSweepPolicy>();
        case EdbEvictionPolicyKind::LruK:
            return std::make_unique<EdbLruKPolicy>(config.lru_k_history);
        case EdbEvictionPolicyKind::Arc:
            return std::make_unique<EdbArcPolicy>();
    }
    return std::make_unique<EdbClockSweepPolicy>();
}

}  // namespace edb