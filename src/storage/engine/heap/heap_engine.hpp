#pragma once

// src/storage/engine/heap/heap_engine.hpp
//
// First concrete storage engine: row-store heap pages with slot arrays.

#include <optional>
#include <span>
#include <map>

#include "storage/buffer/buffer_pool.hpp"
#include "storage/engine/engine_ops.hpp"
#include "transaction/visibility.hpp"
#include "utils/error.hpp"
#include "utils/primitives.hpp"

namespace edb {

class EdbHeapEngine final : public StorageEngineOps {
   public:
    EdbHeapEngine() = default;

    using StorageEngineOps::begin_scan;
    using StorageEngineOps::delete_tuple;
    using StorageEngineOps::insert;
    using StorageEngineOps::update_tuple;

    EdbHeapEngine(const EdbHeapEngine&) = delete;
    EdbHeapEngine& operator=(const EdbHeapEngine&) = delete;
    EdbHeapEngine(EdbHeapEngine&&) = delete;
    EdbHeapEngine& operator=(EdbHeapEngine&&) = delete;

    ~EdbHeapEngine() override = default;

   private:
    auto open_impl(PageStore& store, const EngineConfig& cfg) -> VoidResult override;
    auto close_impl() -> VoidResult override;
    auto insert_impl(std::span<const std::byte> tuple) -> Result<TupleId> override;
    auto insert_impl(const Transaction& tx, std::span<const std::byte> tuple)
        -> Result<TupleId> override;
    auto delete_tuple_impl(TupleId id) -> VoidResult override;
    auto delete_tuple_impl(const Transaction& tx, TupleId id) -> VoidResult override;
    auto update_tuple_impl(TupleId id, std::span<const std::byte> tuple)
        -> Result<TupleId> override;
    auto update_tuple_impl(const Transaction& tx, TupleId id, std::span<const std::byte> tuple)
        -> Result<TupleId> override;
    auto begin_scan_impl() -> Result<ScanHandle> override;
    auto begin_scan_impl(const VisibilityContext& context,
                         const TransactionStatusReader& statuses) -> Result<ScanHandle> override;
    auto scan_next_impl(ScanHandle& handle) -> Result<std::optional<Tuple>> override;
    auto end_scan_impl(ScanHandle& handle) -> VoidResult override;
    [[nodiscard]] auto page_size_impl() const -> usize override;

    [[nodiscard]] auto check_open() const -> VoidResult;
    auto insert_encoded_tuple(std::span<const std::byte> tuple) -> Result<TupleId>;
    auto insert_into_existing_page(u64 page_id, std::span<const std::byte> tuple)
        -> Result<std::optional<TupleId>>;
    auto insert_into_new_page(std::span<const std::byte> tuple) -> Result<TupleId>;
    auto scan_slot(FrameHandle& frame, ScanHandle& handle, u64 page_id, u16 slot_idx)
        -> Result<std::optional<Tuple>>;

    [[nodiscard]] auto mark_deleted(TupleId id, TxId xmax) -> VoidResult;
    [[nodiscard]] auto unpin_clean(FrameHandle& handle) -> VoidResult;

    [[nodiscard]] static auto encode_cursor(u64 page_id, u16 slot_idx) -> u64;
    [[nodiscard]] static auto encode_cursor(u16 scan_id, u64 page_id, u16 slot_idx) -> u64;
    [[nodiscard]] static auto cursor_scan_id(ScanHandle handle) -> u16;
    [[nodiscard]] static auto cursor_page_id(ScanHandle handle) -> u64;
    [[nodiscard]] static auto cursor_slot_idx(ScanHandle handle) -> u16;

    PageStore* page_store{nullptr};
    EngineConfig config{};
    BufferPool buffer_pool;
    struct ScanContext {
        VisibilityContext              context;
        const TransactionStatusReader* statuses{nullptr};
    };
    std::map<u16, ScanContext> scan_contexts;
    u16 next_scan_id{1};
    b8 opened{false};
};

}  // namespace edb
