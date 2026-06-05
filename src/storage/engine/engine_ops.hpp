#pragma once

// src/storage/engine/engine_ops.hpp
//
// Tuple-level storage engine abstraction. Query execution talks to this layer;
// concrete engines decide whether to use the buffer pool, mmap, or specialized
// index files below it.

#include <cstddef>
#include <optional>
#include <span>
#include <vector>

#include "storage/buffer/eviction_policy.hpp"
#include "storage/page/page_store.hpp"
#include "transaction/visibility.hpp"
#include "utils/contracts.hpp"
#include "utils/error.hpp"
#include "utils/primitives.hpp"

namespace edb {

// Forward declaration
struct TupleId;

// ---------------------------------------------------------------------------
// WalEmitter — abstract WAL emission interface
//
// Implemented by the query layer (edb_query) using WalManager. Injected into
// EngineConfig so storage engines can emit WAL records without depending on
// the WAL library directly (which would create a circular library dependency
// since edb_wal already depends on edb_engine).
//
// Contract: the caller (storage engine) must hold the buffer-pool frame pinned
// across the emit_heap_insert call. This ensures the page is not evicted to
// disk before page_lsn is updated to the returned LSN.
// ---------------------------------------------------------------------------
class WalEmitter {
   public:
    WalEmitter() = default;
    WalEmitter(const WalEmitter&) = delete;
    WalEmitter& operator=(const WalEmitter&) = delete;
    WalEmitter(WalEmitter&&) = delete;
    WalEmitter& operator=(WalEmitter&&) = delete;
    virtual ~WalEmitter() = default;

    // Append a HEAP_INSERT WAL record for the tuple at `id`.
    // `stored_bytes` is the complete on-page representation (tuple header +
    // encoded payload). Returns the LSN assigned to the record; the caller
    // must write it to the page as page_lsn before releasing the frame.
    [[nodiscard]] virtual auto emit_heap_insert(TxId tx_id, TupleId id,
                                                std::span<const std::byte> stored_bytes)
        -> Result<u64> = 0;
};

struct EngineConfig {
    usize page_size{8192};
    usize buffer_pool_pages{1024};
    EvictionPolicyConfig buffer_eviction{};
    WalEmitter* wal{nullptr};  // optional; if set, WAL records are emitted for mutations
};

struct TupleId {
    u64 page_id;
    u16 slot_idx;
};

struct Tuple {
    TupleId id;
    std::vector<std::byte> data;
};

struct ScanHandle {
    u64 value;
};

struct StorageEngineOps {
    StorageEngineOps() = default;

    StorageEngineOps(const StorageEngineOps&) = delete;
    StorageEngineOps& operator=(const StorageEngineOps&) = delete;
    StorageEngineOps(StorageEngineOps&&) = delete;
    StorageEngineOps& operator=(StorageEngineOps&&) = delete;

    virtual ~StorageEngineOps() = default;

    auto open(PageStore& store, const EngineConfig& cfg) -> VoidResult
        EDB_PRE(cfg.page_size > usize{0}) {
        return open_impl(store, cfg);
    }

    auto close() -> VoidResult { return close_impl(); }

    auto insert(std::span<const std::byte> tuple) -> Result<TupleId> EDB_PRE(!tuple.empty()) {
        return insert_impl(tuple);
    }

    auto insert(const Transaction& tx, std::span<const std::byte> tuple)
        -> Result<TupleId> EDB_PRE(!tuple.empty()) {
        return insert_impl(tx, tuple);
    }

    auto delete_tuple(TupleId id) -> VoidResult { return delete_tuple_impl(id); }

    auto delete_tuple(const Transaction& tx, TupleId id) -> VoidResult {
        return delete_tuple_impl(tx, id);
    }

    auto update_tuple(TupleId id, std::span<const std::byte> tuple)
        -> Result<TupleId> EDB_PRE(!tuple.empty()) {
        return update_tuple_impl(id, tuple);
    }

    auto update_tuple(const Transaction& tx, TupleId id, std::span<const std::byte> tuple)
        -> Result<TupleId> EDB_PRE(!tuple.empty()) {
        return update_tuple_impl(tx, id, tuple);
    }

    auto begin_scan() -> Result<ScanHandle> { return begin_scan_impl(); }

    auto begin_scan(const VisibilityContext& context, const TransactionStatusReader& statuses)
        -> Result<ScanHandle> {
        return begin_scan_impl(context, statuses);
    }

    auto scan_next(ScanHandle& handle) -> Result<std::optional<Tuple>> {
        return scan_next_impl(handle);
    }

    auto end_scan(ScanHandle& handle) -> VoidResult { return end_scan_impl(handle); }

    [[nodiscard]] auto page_size() const -> usize { return page_size_impl(); }

   protected:
    virtual auto open_impl(PageStore& store, const EngineConfig& cfg) -> VoidResult = 0;
    virtual auto close_impl() -> VoidResult = 0;
    virtual auto insert_impl(std::span<const std::byte> tuple) -> Result<TupleId> = 0;
    virtual auto insert_impl(const Transaction& /*tx*/, std::span<const std::byte> tuple)
        -> Result<TupleId> {
        return insert_impl(tuple);
    }
    virtual auto delete_tuple_impl(TupleId id) -> VoidResult = 0;
    virtual auto delete_tuple_impl(const Transaction& /*tx*/, TupleId id) -> VoidResult {
        return delete_tuple_impl(id);
    }
    virtual auto update_tuple_impl(TupleId id, std::span<const std::byte> tuple)
        -> Result<TupleId> = 0;
    virtual auto update_tuple_impl(const Transaction& /*tx*/, TupleId id,
                                   std::span<const std::byte> tuple) -> Result<TupleId> {
        return update_tuple_impl(id, tuple);
    }
    virtual auto begin_scan_impl() -> Result<ScanHandle> = 0;
    virtual auto begin_scan_impl(const VisibilityContext& /*context*/,
                                 const TransactionStatusReader& /*statuses*/)
        -> Result<ScanHandle> {
        return begin_scan_impl();
    }
    virtual auto scan_next_impl(ScanHandle& handle) -> Result<std::optional<Tuple>> = 0;
    virtual auto end_scan_impl(ScanHandle& handle) -> VoidResult = 0;
    [[nodiscard]] virtual auto page_size_impl() const -> usize = 0;
};

}  // namespace edb
