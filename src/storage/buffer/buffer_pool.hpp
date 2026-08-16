#pragma once

// src/storage/buffer/buffer_pool.hpp
//
// Format-agnostic page cache over EdbPageStore. Storage engines pin frames,
// inspect/mutate raw page bytes, then explicitly unpin with a dirty flag.

#include <cstddef>
#include <memory>
#include <span>
#include <vector>

#include "storage/buffer/eviction_policy.hpp"
#include "storage/page/page_store.hpp"
#include "utils/assert.hpp"
#include "utils/error.hpp"
#include "utils/primitives.hpp"

namespace edb {

struct BufferPoolConfig {
    usize capacity_pages{1024};
    EvictionPolicyConfig eviction{};
};

class BufferPool;

class FrameHandle {
   public:
    FrameHandle() = default;
    FrameHandle(BufferPool* owner, usize frame_index, u64 page_id, std::span<std::byte> bytes);

    FrameHandle(const FrameHandle&) = delete;
    FrameHandle& operator=(const FrameHandle&) = delete;
    FrameHandle(FrameHandle&& other) noexcept;
    FrameHandle& operator=(FrameHandle&& other) noexcept;
    ~FrameHandle();

    [[nodiscard]] auto data() const -> std::span<std::byte>;
    [[nodiscard]] auto page_id() const -> u64;
    [[nodiscard]] auto is_valid() const -> b8;

   private:
    friend class BufferPool;

    auto release() -> void;

    BufferPool* pool{nullptr};
    usize index{0};
    u64 id{0};
    std::span<std::byte> bytes;
};

class BufferPool {
   public:
    BufferPool() = default;

    BufferPool(const BufferPool&) = delete;
    BufferPool& operator=(const BufferPool&) = delete;
    BufferPool(BufferPool&&) = delete;
    BufferPool& operator=(BufferPool&&) = delete;

    ~BufferPool() = default;

    auto open(PageStore& page_store, const BufferPoolConfig& cfg) -> VoidResult;
    auto close() -> VoidResult;

    auto fetch(u64 page_id) -> Result<FrameHandle>;
    auto fetch_new(u64 page_id) -> Result<FrameHandle>;

    auto unpin(FrameHandle& handle, b8 dirty) -> VoidResult;
    auto flush(u64 page_id) -> VoidResult;
    auto flush_all() -> VoidResult;

    [[nodiscard]] auto capacity() const -> usize;
    [[nodiscard]] auto page_size() const -> usize;

   private:
    struct Frame {
        u64 page_id{0};
        std::vector<std::byte> data;
        b8 valid{false};
        b8 dirty{false};
        usize pin_count{0};
    };

    [[nodiscard]] auto check_open() const -> VoidResult;
    [[nodiscard]] auto find_frame(u64 page_id) -> Result<usize>;
    [[nodiscard]] auto choose_victim() -> Result<usize>;
    auto write_back_if_dirty(Frame& frame) -> VoidResult;
    auto load_page_into_frame(usize frame_index, u64 page_id) -> VoidResult;
    auto load_blank_page_into_frame(usize frame_index, u64 page_id) -> VoidResult;
    [[nodiscard]] auto make_handle(usize frame_index) -> FrameHandle;

    PageStore* store{nullptr};
    BufferPoolConfig config{};
    usize page_bytes{0};
    std::vector<Frame> frames;
    std::unique_ptr<EvictionPolicy> eviction_policy;
};

}  // namespace edb
