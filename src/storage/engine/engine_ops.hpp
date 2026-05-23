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

#include "storage/page/page_store.hpp"
#include "utils/contracts.hpp"
#include "utils/error.hpp"
#include "utils/primitives.hpp"

namespace edb {

struct EdbEngineConfig {
    usize page_size{8192};
    usize buffer_pool_pages{1024};
};

struct EdbTupleId {
    u64 page_id;
    u16 slot_idx;
};

struct EdbTuple {
    EdbTupleId id;
    std::vector<std::byte> data;
};

struct EdbScanHandle {
    u64 value;
};

struct EdbStorageEngineOps {
    EdbStorageEngineOps() = default;

    EdbStorageEngineOps(const EdbStorageEngineOps&) = delete;
    EdbStorageEngineOps& operator=(const EdbStorageEngineOps&) = delete;
    EdbStorageEngineOps(EdbStorageEngineOps&&) = delete;
    EdbStorageEngineOps& operator=(EdbStorageEngineOps&&) = delete;

    virtual ~EdbStorageEngineOps() = default;

    auto open(EdbPageStore& store, const EdbEngineConfig& cfg) -> EdbStatus
        EDB_PRE(cfg.page_size > usize{0}) {
        return open_impl(store, cfg);
    }

    auto close() -> EdbStatus { return close_impl(); }

    auto insert(std::span<const std::byte> tuple) -> EdbResult<EdbTupleId> EDB_PRE(!tuple.empty()) {
        return insert_impl(tuple);
    }

    auto delete_tuple(EdbTupleId id) -> EdbStatus { return delete_tuple_impl(id); }

    auto update_tuple(EdbTupleId id, std::span<const std::byte> tuple)
        -> EdbResult<EdbTupleId> EDB_PRE(!tuple.empty()) {
        return update_tuple_impl(id, tuple);
    }

    auto begin_scan() -> EdbResult<EdbScanHandle> { return begin_scan_impl(); }

    auto scan_next(EdbScanHandle& handle) -> EdbResult<std::optional<EdbTuple>> {
        return scan_next_impl(handle);
    }

    auto end_scan(EdbScanHandle& handle) -> EdbStatus { return end_scan_impl(handle); }

    [[nodiscard]] auto page_size() const -> usize { return page_size_impl(); }

   protected:
    virtual auto open_impl(EdbPageStore& store, const EdbEngineConfig& cfg) -> EdbStatus = 0;
    virtual auto close_impl() -> EdbStatus = 0;
    virtual auto insert_impl(std::span<const std::byte> tuple) -> EdbResult<EdbTupleId> = 0;
    virtual auto delete_tuple_impl(EdbTupleId id) -> EdbStatus = 0;
    virtual auto update_tuple_impl(EdbTupleId id, std::span<const std::byte> tuple)
        -> EdbResult<EdbTupleId> = 0;
    virtual auto begin_scan_impl() -> EdbResult<EdbScanHandle> = 0;
    virtual auto scan_next_impl(EdbScanHandle& handle) -> EdbResult<std::optional<EdbTuple>> = 0;
    virtual auto end_scan_impl(EdbScanHandle& handle) -> EdbStatus = 0;
    [[nodiscard]] virtual auto page_size_impl() const -> usize = 0;
};

}  // namespace edb
