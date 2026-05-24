// src/storage/buffer/buffer_pool.cpp

#include "storage/buffer/buffer_pool.hpp"

#include <algorithm>
#include <expected>
#include <utility>

namespace edb {

FrameHandle::FrameHandle(BufferPool* owner, usize frame_index, u64 page_id,
                         std::span<std::byte> bytes_view)
    : pool{owner}, index{frame_index}, id{page_id}, bytes{bytes_view} {}

FrameHandle::FrameHandle(FrameHandle&& other) noexcept
    : pool{std::exchange(other.pool, nullptr)},
      index{std::exchange(other.index, usize{0})},
      id{std::exchange(other.id, u64{0})},
      bytes{std::exchange(other.bytes, std::span<std::byte>{})} {}

FrameHandle& FrameHandle::operator=(FrameHandle&& other) noexcept {
    if (this != &other) {
        release();
        pool = std::exchange(other.pool, nullptr);
        index = std::exchange(other.index, usize{0});
        id = std::exchange(other.id, u64{0});
        bytes = std::exchange(other.bytes, std::span<std::byte>{});
    }
    return *this;
}

FrameHandle::~FrameHandle() {
    release();
}

auto FrameHandle::data() const -> std::span<std::byte> {
    return bytes;
}

auto FrameHandle::page_id() const -> u64 {
    return id;
}

auto FrameHandle::is_valid() const -> b8 {
    return b8{pool != nullptr};
}

auto FrameHandle::release() -> void {
    pool = nullptr;
    index = usize{0};
    id = u64{0};
    bytes = {};
}

auto BufferPool::open(PageStore& page_store, const BufferPoolConfig& cfg) -> VoidResult {
    store = &page_store;
    config = cfg;
    page_bytes = page_store.page_size();
    eviction_policy = make_eviction_policy(cfg.eviction);
    if (auto status = eviction_policy->reset(cfg.capacity_pages); !status) {
        return status;
    }
    frames.clear();
    frames.reserve(cfg.capacity_pages.value);
    for (usize frame_index{0}; frame_index < cfg.capacity_pages; ++frame_index) {
        auto& frame = frames.emplace_back();
        frame.data.resize(page_bytes.value);
    }
    return {};
}

auto BufferPool::close() -> VoidResult {
    if (store != nullptr) {
        if (auto status = flush_all(); !status) {
            return status;
        }
    }
    frames.clear();
    eviction_policy.reset();
    store = nullptr;
    config = {};
    page_bytes = usize{0};
    return {};
}

auto BufferPool::fetch(u64 page_id) -> Result<FrameHandle> {
    if (auto status = check_open(); !status) {
        return std::unexpected(status.error());
    }

    auto cached = find_frame(page_id);
    if (cached) {
        auto& frame = frames[cached->value];
        ++frame.pin_count;
        if (auto status = eviction_policy->record_access(page_id, *cached); !status) {
            return std::unexpected(status.error());
        }
        return make_handle(*cached);
    }

    if (auto status = eviction_policy->record_miss(page_id); !status) {
        return std::unexpected(status.error());
    }

    auto victim = choose_victim();
    if (!victim) {
        return std::unexpected(victim.error());
    }
    if (auto status = load_page_into_frame(*victim, page_id); !status) {
        return std::unexpected(status.error());
    }
    return make_handle(*victim);
}

auto BufferPool::fetch_new(u64 page_id) -> Result<FrameHandle> {
    if (auto status = check_open(); !status) {
        return std::unexpected(status.error());
    }

    auto cached = find_frame(page_id);
    if (cached) {
        auto& frame = frames[cached->value];
        ++frame.pin_count;
        frame.dirty = b8{true};
        std::ranges::fill(frame.data, std::byte{0});
        if (auto status = eviction_policy->record_access(page_id, *cached); !status) {
            return std::unexpected(status.error());
        }
        return make_handle(*cached);
    }

    if (auto status = eviction_policy->record_miss(page_id); !status) {
        return std::unexpected(status.error());
    }

    auto victim = choose_victim();
    if (!victim) {
        return std::unexpected(victim.error());
    }
    if (auto status = load_blank_page_into_frame(*victim, page_id); !status) {
        return std::unexpected(status.error());
    }
    return make_handle(*victim);
}

auto BufferPool::unpin(FrameHandle& handle, b8 dirty) -> VoidResult {
    if (auto status = check_open(); !status) {
        return status;
    }
    if (handle.pool != this || handle.index >= usize{frames.size()}) {
        return std::unexpected(Error::InvalidArgument);
    }

    auto& frame = frames[handle.index.value];
    if (!frame.valid.value || frame.pin_count == usize{0}) {
        return std::unexpected(Error::InvalidArgument);
    }
    if (dirty.value) {
        frame.dirty = b8{true};
    }
    --frame.pin_count;
    handle.release();
    return {};
}

auto BufferPool::flush(u64 page_id) -> VoidResult {
    if (auto status = check_open(); !status) {
        return status;
    }

    auto cached = find_frame(page_id);
    if (!cached) {
        return {};
    }
    return write_back_if_dirty(frames[cached->value]);
}

auto BufferPool::flush_all() -> VoidResult {
    if (auto status = check_open(); !status) {
        return status;
    }

    for (auto& frame : frames) {
        if (auto status = write_back_if_dirty(frame); !status) {
            return status;
        }
    }
    return store->sync();
}

auto BufferPool::capacity() const -> usize {
    return config.capacity_pages;
}

auto BufferPool::page_size() const -> usize {
    return page_bytes;
}

auto BufferPool::check_open() const -> VoidResult {
    if (store == nullptr) {
        return std::unexpected(Error::IoError);
    }
    return {};
}

auto BufferPool::find_frame(u64 page_id) -> Result<usize> {
    for (usize frame_index{0}; frame_index < usize{frames.size()}; ++frame_index) {
        const auto& frame = frames[frame_index.value];
        if (frame.valid.value && frame.page_id == page_id) {
            return frame_index;
        }
    }
    return std::unexpected(Error::NotFound);
}

auto BufferPool::choose_victim() -> Result<usize> {
    for (usize frame_index{0}; frame_index < usize{frames.size()}; ++frame_index) {
        if (!frames[frame_index.value].valid.value) {
            return frame_index;
        }
    }

    if (eviction_policy == nullptr) {
        return std::unexpected(Error::IoError);
    }

    std::vector<EvictionFrameState> states;
    states.reserve(frames.size());
    for (const auto& frame : frames) {
        states.push_back(EvictionFrameState{.page_id = frame.page_id,
                                            .valid = frame.valid,
                                            .pinned = b8{frame.pin_count > usize{0}}});
    }
    return eviction_policy->choose_victim(states);
}

auto BufferPool::write_back_if_dirty(Frame& frame) -> VoidResult {
    if (!frame.valid.value || !frame.dirty.value) {
        return {};
    }
    auto status = store->write_page(frame.page_id, frame.data);
    if (!status) {
        return status;
    }
    frame.dirty = b8{false};
    return {};
}

auto BufferPool::load_page_into_frame(usize frame_index, u64 page_id) -> VoidResult {
    auto& frame = frames[frame_index.value];
    const auto evicted_page_id = frame.page_id;
    const auto had_valid_page = frame.valid;
    if (auto status = write_back_if_dirty(frame); !status) {
        return status;
    }
    auto status = store->read_page(page_id, frame.data);
    if (!status) {
        return status;
    }
    if (had_valid_page.value) {
        if (auto evict_status = eviction_policy->record_evict(evicted_page_id, frame_index);
            !evict_status) {
            return evict_status;
        }
    }
    frame.page_id = page_id;
    frame.valid = b8{true};
    frame.dirty = b8{false};
    frame.pin_count = usize{1};
    return eviction_policy->record_load(page_id, frame_index);
}

auto BufferPool::load_blank_page_into_frame(usize frame_index, u64 page_id) -> VoidResult {
    auto& frame = frames[frame_index.value];
    const auto evicted_page_id = frame.page_id;
    const auto had_valid_page = frame.valid;
    if (auto status = write_back_if_dirty(frame); !status) {
        return status;
    }
    if (had_valid_page.value) {
        if (auto evict_status = eviction_policy->record_evict(evicted_page_id, frame_index);
            !evict_status) {
            return evict_status;
        }
    }
    std::ranges::fill(frame.data, std::byte{0});
    frame.page_id = page_id;
    frame.valid = b8{true};
    frame.dirty = b8{true};
    frame.pin_count = usize{1};
    return eviction_policy->record_load(page_id, frame_index);
}

auto BufferPool::make_handle(usize frame_index) -> FrameHandle {
    auto& frame = frames[frame_index.value];
    return FrameHandle{this, frame_index, frame.page_id, frame.data};
}

}  // namespace edb
