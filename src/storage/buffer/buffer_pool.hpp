#pragma once

// src/storage/buffer/buffer_pool.hpp
//
// Format-agnostic page cache over EdbPageStore. Storage engines pin frames,
// inspect/mutate raw page bytes, then explicitly unpin with a dirty flag.

#include <cstddef>
#include <span>
#include <vector>

#include "storage/page/page_store.hpp"
#include "utils/contracts.hpp"
#include "utils/error.hpp"
#include "utils/primitives.hpp"

namespace edb {

struct EdbBufferPoolConfig {
    usize capacity_pages{1024};
};

class EdbBufferPool;

class EdbFrameHandle {
   public:
    EdbFrameHandle() = default;
    EdbFrameHandle(EdbBufferPool* owner, usize frame_index, u64 page_id,
                   std::span<std::byte> bytes);

    EdbFrameHandle(const EdbFrameHandle&) = delete;
    EdbFrameHandle& operator=(const EdbFrameHandle&) = delete;
    EdbFrameHandle(EdbFrameHandle&& other) noexcept;
    EdbFrameHandle& operator=(EdbFrameHandle&& other) noexcept;
    ~EdbFrameHandle();

    [[nodiscard]] auto data() const -> std::span<std::byte>;
    [[nodiscard]] auto page_id() const -> u64;
    [[nodiscard]] auto is_valid() const -> b8;

   private:
    friend class EdbBufferPool;

    auto release() -> void;

    EdbBufferPool* pool{nullptr};
    usize index{0};
    u64 id{0};
    std::span<std::byte> bytes;
};

class EdbBufferPool {
   public:
    EdbBufferPool() = default;

    EdbBufferPool(const EdbBufferPool&) = delete;
    EdbBufferPool& operator=(const EdbBufferPool&) = delete;
    EdbBufferPool(EdbBufferPool&&) = delete;
    EdbBufferPool& operator=(EdbBufferPool&&) = delete;

    ~EdbBufferPool() = default;

    auto open(EdbPageStore& page_store, const EdbBufferPoolConfig& cfg) -> EdbStatus
        EDB_PRE(cfg.capacity_pages > usize{0});
    auto close() -> EdbStatus;

    auto fetch(u64 page_id) -> EdbResult<EdbFrameHandle>;
    auto fetch_new(u64 page_id) -> EdbResult<EdbFrameHandle>;

    auto unpin(EdbFrameHandle& handle, b8 dirty) -> EdbStatus;
    auto flush(u64 page_id) -> EdbStatus;
    auto flush_all() -> EdbStatus;

    [[nodiscard]] auto capacity() const -> usize;
    [[nodiscard]] auto page_size() const -> usize;

   private:
    struct Frame {
        u64 page_id{0};
        std::vector<std::byte> data;
        b8 valid{false};
        b8 dirty{false};
        b8 referenced{false};
        usize pin_count{0};
    };

    [[nodiscard]] auto check_open() const -> EdbStatus;
    [[nodiscard]] auto find_frame(u64 page_id) -> EdbResult<usize>;
    [[nodiscard]] auto choose_victim() -> EdbResult<usize>;
    auto write_back_if_dirty(Frame& frame) -> EdbStatus;
    auto load_page_into_frame(usize frame_index, u64 page_id) -> EdbStatus;
    auto load_blank_page_into_frame(usize frame_index, u64 page_id) -> EdbStatus;
    [[nodiscard]] auto make_handle(usize frame_index) -> EdbFrameHandle;

    EdbPageStore* store{nullptr};
    EdbBufferPoolConfig config{};
    usize page_bytes{0};
    std::vector<Frame> frames;
    usize clock_hand{0};
};

}  // namespace edb
