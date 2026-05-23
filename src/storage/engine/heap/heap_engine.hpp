#pragma once

// src/storage/engine/heap/heap_engine.hpp
//
// First concrete storage engine: row-store heap pages with slot arrays.

#include <optional>
#include <span>

#include "storage/buffer/buffer_pool.hpp"
#include "storage/engine/engine_ops.hpp"
#include "utils/error.hpp"
#include "utils/primitives.hpp"

namespace edb {

class EdbHeapEngine final : public EdbStorageEngineOps {
   public:
    EdbHeapEngine() = default;

    EdbHeapEngine(const EdbHeapEngine&) = delete;
    EdbHeapEngine& operator=(const EdbHeapEngine&) = delete;
    EdbHeapEngine(EdbHeapEngine&&) = delete;
    EdbHeapEngine& operator=(EdbHeapEngine&&) = delete;

    ~EdbHeapEngine() override = default;

   private:
    auto open_impl(EdbPageStore& store, const EdbEngineConfig& cfg) -> EdbStatus override;
    auto close_impl() -> EdbStatus override;
    auto insert_impl(std::span<const std::byte> tuple) -> EdbResult<EdbTupleId> override;
    auto delete_tuple_impl(EdbTupleId id) -> EdbStatus override;
    auto update_tuple_impl(EdbTupleId id, std::span<const std::byte> tuple)
        -> EdbResult<EdbTupleId> override;
    auto begin_scan_impl() -> EdbResult<EdbScanHandle> override;
    auto scan_next_impl(EdbScanHandle& handle) -> EdbResult<std::optional<EdbTuple>> override;
    auto end_scan_impl(EdbScanHandle& handle) -> EdbStatus override;
    [[nodiscard]] auto page_size_impl() const -> usize override;

    [[nodiscard]] auto check_open() const -> EdbStatus;
    auto insert_into_existing_page(u64 page_id, std::span<const std::byte> tuple)
        -> EdbResult<std::optional<EdbTupleId>>;
    auto insert_into_new_page(std::span<const std::byte> tuple) -> EdbResult<EdbTupleId>;

    [[nodiscard]] static auto encode_cursor(u64 page_id, u16 slot_idx) -> u64;
    [[nodiscard]] static auto cursor_page_id(EdbScanHandle handle) -> u64;
    [[nodiscard]] static auto cursor_slot_idx(EdbScanHandle handle) -> u16;

    EdbPageStore* page_store{nullptr};
    EdbEngineConfig config{};
    EdbBufferPool buffer_pool;
    b8 opened{false};
};

}  // namespace edb
