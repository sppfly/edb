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

auto is_evictable(std::span<const EvictionFrameState> frames, usize frame_index) -> b8 {
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
                      std::unordered_map<u64, ArcPolicy::Location>& locations) -> void {
    if (ghosts.empty()) {
        return;
    }
    const auto page_id = ghosts.back();
    ghosts.pop_back();
    locations.erase(page_id);
}

}  // namespace

auto ClockSweepPolicy::reset_impl(usize capacity) -> VoidResult {
    referenced.assign(capacity.value, b8{false});
    clock_hand = usize{0};
    return {};
}

auto ClockSweepPolicy::record_access_impl(u64 /*page_id*/, usize frame_index) -> VoidResult {
    if (frame_index >= usize{referenced.size()}) {
        return std::unexpected(Error::InvalidArgument);
    }
    referenced[frame_index.value] = b8{true};
    return {};
}

auto ClockSweepPolicy::record_miss_impl(u64 /*page_id*/) -> VoidResult {
    return {};
}

auto ClockSweepPolicy::record_load_impl(u64 page_id, usize frame_index) -> VoidResult {
    return record_access_impl(page_id, frame_index);
}

auto ClockSweepPolicy::record_evict_impl(u64 /*page_id*/, usize frame_index) -> VoidResult {
    if (frame_index >= usize{referenced.size()}) {
        return std::unexpected(Error::InvalidArgument);
    }
    referenced[frame_index.value] = b8{false};
    return {};
}

auto ClockSweepPolicy::choose_victim_impl(std::span<const EvictionFrameState> frames)
    -> Result<usize> {
    if (frames.empty() || referenced.size() != frames.size()) {
        return std::unexpected(Error::InvalidArgument);
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
    return std::unexpected(Error::BufferPoolFull);
}

LruKPolicy::LruKPolicy(usize history_count) : k_history{history_count} {}

auto LruKPolicy::reset_impl(usize /*capacity*/) -> VoidResult {
    tick = u64{0};
    histories.clear();
    return {};
}

auto LruKPolicy::record_access_impl(u64 page_id, usize /*frame_index*/) -> VoidResult {
    ++tick;
    auto& history = histories[page_id];
    history.push_back(tick);
    while (history.size() > k_history.value) {
        history.pop_front();
    }
    return {};
}

auto LruKPolicy::record_miss_impl(u64 /*page_id*/) -> VoidResult {
    return {};
}

auto LruKPolicy::record_load_impl(u64 page_id, usize frame_index) -> VoidResult {
    return record_access_impl(page_id, frame_index);
}

auto LruKPolicy::record_evict_impl(u64 /*page_id*/, usize /*frame_index*/) -> VoidResult {
    return {};
}

auto LruKPolicy::choose_victim_impl(std::span<const EvictionFrameState> frames)
    -> Result<usize> {
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

auto ArcPolicy::reset_impl(usize new_capacity) -> VoidResult {
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

auto ArcPolicy::record_access_impl(u64 page_id, usize frame_index) -> VoidResult {
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

auto ArcPolicy::record_miss_impl(u64 page_id) -> VoidResult {
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

auto ArcPolicy::record_load_impl(u64 page_id, usize frame_index) -> VoidResult {
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

auto ArcPolicy::record_evict_impl(u64 page_id, usize /*frame_index*/) -> VoidResult {
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

auto ArcPolicy::choose_victim_impl(std::span<const EvictionFrameState> frames)
    -> Result<usize> {
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
    return std::unexpected(Error::BufferPoolFull);
}

auto ArcPolicy::choose_from_lru(std::list<u64>& pages,
                                   std::span<const EvictionFrameState> frames)
    -> Result<usize> {
    for (auto iter = pages.rbegin(); iter != pages.rend(); ++iter) {
        const auto frame = resident_frames.find(*iter);
        if (frame != resident_frames.end() && is_evictable(frames, frame->second).value) {
            return frame->second;
        }
    }
    return std::unexpected(Error::NotFound);
}

auto ArcPolicy::prune_ghosts() -> void {
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

auto make_eviction_policy(const EvictionPolicyConfig& config)
    -> std::unique_ptr<EvictionPolicy> {
    switch (config.kind) {
        case EvictionPolicyKind::ClockSweep:
            return std::make_unique<ClockSweepPolicy>();
        case EvictionPolicyKind::LruK:
            return std::make_unique<LruKPolicy>(config.lru_k_history);
        case EvictionPolicyKind::Arc:
            return std::make_unique<ArcPolicy>();
    }
    return std::make_unique<ClockSweepPolicy>();
}

}  // namespace edb