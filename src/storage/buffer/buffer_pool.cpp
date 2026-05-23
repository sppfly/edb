// src/storage/buffer/buffer_pool.cpp

#include "storage/buffer/buffer_pool.hpp"

#include <algorithm>
#include <expected>
#include <utility>

namespace edb {

EdbFrameHandle::EdbFrameHandle(EdbBufferPool* owner, usize frame_index, u64 page_id,
                               std::span<std::byte> bytes_view)
    : pool{owner}, index{frame_index}, id{page_id}, bytes{bytes_view} {}

EdbFrameHandle::EdbFrameHandle(EdbFrameHandle&& other) noexcept
    : pool{std::exchange(other.pool, nullptr)},
      index{std::exchange(other.index, usize{0})},
      id{std::exchange(other.id, u64{0})},
      bytes{std::exchange(other.bytes, std::span<std::byte>{})} {}

EdbFrameHandle& EdbFrameHandle::operator=(EdbFrameHandle&& other) noexcept {
    if (this != &other) {
        release();
        pool = std::exchange(other.pool, nullptr);
        index = std::exchange(other.index, usize{0});
        id = std::exchange(other.id, u64{0});
        bytes = std::exchange(other.bytes, std::span<std::byte>{});
    }
    return *this;
}

EdbFrameHandle::~EdbFrameHandle() {
    release();
}

auto EdbFrameHandle::data() const -> std::span<std::byte> {
    return bytes;
}

auto EdbFrameHandle::page_id() const -> u64 {
    return id;
}

auto EdbFrameHandle::is_valid() const -> b8 {
    return b8{pool != nullptr};
}

auto EdbFrameHandle::release() -> void {
    pool = nullptr;
    index = usize{0};
    id = u64{0};
    bytes = {};
}

auto EdbBufferPool::open(EdbPageStore& page_store, const EdbBufferPoolConfig& cfg) -> EdbStatus {
    store = &page_store;
    config = cfg;
    page_bytes = page_store.page_size();
    clock_hand = usize{0};
    frames.clear();
    frames.reserve(cfg.capacity_pages.value);
    for (usize frame_index{0}; frame_index < cfg.capacity_pages; ++frame_index) {
        auto& frame = frames.emplace_back();
        frame.data.resize(page_bytes.value);
    }
    return {};
}

auto EdbBufferPool::close() -> EdbStatus {
    if (store != nullptr) {
        if (auto status = flush_all(); !status) {
            return status;
        }
    }
    frames.clear();
    store = nullptr;
    config = {};
    page_bytes = usize{0};
    clock_hand = usize{0};
    return {};
}

auto EdbBufferPool::fetch(u64 page_id) -> EdbResult<EdbFrameHandle> {
    if (auto status = check_open(); !status) {
        return std::unexpected(status.error());
    }

    auto cached = find_frame(page_id);
    if (cached) {
        auto& frame = frames[cached->value];
        ++frame.pin_count;
        frame.referenced = b8{true};
        return make_handle(*cached);
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

auto EdbBufferPool::fetch_new(u64 page_id) -> EdbResult<EdbFrameHandle> {
    if (auto status = check_open(); !status) {
        return std::unexpected(status.error());
    }

    auto cached = find_frame(page_id);
    if (cached) {
        auto& frame = frames[cached->value];
        ++frame.pin_count;
        frame.referenced = b8{true};
        frame.dirty = b8{true};
        std::ranges::fill(frame.data, std::byte{0});
        return make_handle(*cached);
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

auto EdbBufferPool::unpin(EdbFrameHandle& handle, b8 dirty) -> EdbStatus {
    if (auto status = check_open(); !status) {
        return status;
    }
    if (handle.pool != this || handle.index >= usize{frames.size()}) {
        return std::unexpected(EdbError::InvalidArgument);
    }

    auto& frame = frames[handle.index.value];
    if (!frame.valid.value || frame.pin_count == usize{0}) {
        return std::unexpected(EdbError::InvalidArgument);
    }
    if (dirty.value) {
        frame.dirty = b8{true};
    }
    --frame.pin_count;
    handle.release();
    return {};
}

auto EdbBufferPool::flush(u64 page_id) -> EdbStatus {
    if (auto status = check_open(); !status) {
        return status;
    }

    auto cached = find_frame(page_id);
    if (!cached) {
        return {};
    }
    return write_back_if_dirty(frames[cached->value]);
}

auto EdbBufferPool::flush_all() -> EdbStatus {
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

auto EdbBufferPool::capacity() const -> usize {
    return config.capacity_pages;
}

auto EdbBufferPool::page_size() const -> usize {
    return page_bytes;
}

auto EdbBufferPool::check_open() const -> EdbStatus {
    if (store == nullptr) {
        return std::unexpected(EdbError::IoError);
    }
    return {};
}

auto EdbBufferPool::find_frame(u64 page_id) -> EdbResult<usize> {
    for (usize frame_index{0}; frame_index < usize{frames.size()}; ++frame_index) {
        const auto& frame = frames[frame_index.value];
        if (frame.valid.value && frame.page_id == page_id) {
            return frame_index;
        }
    }
    return std::unexpected(EdbError::NotFound);
}

auto EdbBufferPool::choose_victim() -> EdbResult<usize> {
    for (usize frame_index{0}; frame_index < usize{frames.size()}; ++frame_index) {
        if (!frames[frame_index.value].valid.value) {
            return frame_index;
        }
    }

    const auto scan_limit = usize{frames.size() * 2U};
    for (usize scans{0}; scans < scan_limit; ++scans) {
        auto& frame = frames[clock_hand.value];
        if (frame.pin_count == usize{0}) {
            if (frame.referenced.value) {
                frame.referenced = b8{false};
            } else {
                const auto victim = clock_hand;
                clock_hand = usize{(clock_hand.value + 1U) % frames.size()};
                return victim;
            }
        }
        clock_hand = usize{(clock_hand.value + 1U) % frames.size()};
    }
    return std::unexpected(EdbError::BufferPoolFull);
}

auto EdbBufferPool::write_back_if_dirty(Frame& frame) -> EdbStatus {
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

auto EdbBufferPool::load_page_into_frame(usize frame_index, u64 page_id) -> EdbStatus {
    auto& frame = frames[frame_index.value];
    if (auto status = write_back_if_dirty(frame); !status) {
        return status;
    }
    auto status = store->read_page(page_id, frame.data);
    if (!status) {
        return status;
    }
    frame.page_id = page_id;
    frame.valid = b8{true};
    frame.dirty = b8{false};
    frame.referenced = b8{true};
    frame.pin_count = usize{1};
    return {};
}

auto EdbBufferPool::load_blank_page_into_frame(usize frame_index, u64 page_id) -> EdbStatus {
    auto& frame = frames[frame_index.value];
    if (auto status = write_back_if_dirty(frame); !status) {
        return status;
    }
    std::ranges::fill(frame.data, std::byte{0});
    frame.page_id = page_id;
    frame.valid = b8{true};
    frame.dirty = b8{true};
    frame.referenced = b8{true};
    frame.pin_count = usize{1};
    return {};
}

auto EdbBufferPool::make_handle(usize frame_index) -> EdbFrameHandle {
    auto& frame = frames[frame_index.value];
    return EdbFrameHandle{this, frame_index, frame.page_id, frame.data};
}

}  // namespace edb
