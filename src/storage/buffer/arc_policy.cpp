// src/storage/buffer/arc_policy.cpp

#include "storage/buffer/arc_policy.hpp"

#include <algorithm>
#include <expected>
#include <list>
#include <ranges>
#include <unordered_map>

#include "utils/assert.hpp"

namespace edb {

namespace {

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

auto ArcPolicy::reset(usize new_capacity) -> VoidResult {
    EDB_ASSERT(new_capacity > usize{0});
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

auto ArcPolicy::record_access(u64 page_id, usize frame_index) -> VoidResult {
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

auto ArcPolicy::record_miss(u64 page_id) -> VoidResult {
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

auto ArcPolicy::record_load(u64 page_id, usize frame_index) -> VoidResult {
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

auto ArcPolicy::record_evict(u64 page_id, usize /*frame_index*/) -> VoidResult {
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

auto ArcPolicy::choose_victim(std::span<const EvictionFrameState> frames) -> Result<usize> {
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

auto ArcPolicy::choose_from_lru(std::list<u64>& pages, std::span<const EvictionFrameState> frames)
    -> Result<usize> {
    for (auto& page : std::views::reverse(pages)) {
        const auto frame = resident_frames.find(page);
        if (frame != resident_frames.end() && detail::is_evictable(frames, frame->second).value) {
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

}  // namespace edb
