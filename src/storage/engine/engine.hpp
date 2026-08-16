#pragma once

// src/storage/engine/engine.hpp
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
#include "utils/error.hpp"
#include "utils/primitives.hpp"

namespace edb {

// Forward declaration
struct TupleId;

struct EngineConfig {
    usize page_size{8192};
    usize buffer_pool_pages{1024};
    EvictionPolicyConfig buffer_eviction{};
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

struct StorageEngine {
    StorageEngine() = default;

    StorageEngine(const StorageEngine&) = delete;
    StorageEngine& operator=(const StorageEngine&) = delete;
    StorageEngine(StorageEngine&&) = delete;
    StorageEngine& operator=(StorageEngine&&) = delete;

    virtual ~StorageEngine() = default;

    virtual auto open(PageStore& store, const EngineConfig& cfg) -> VoidResult = 0;
    virtual auto close() -> VoidResult = 0;

    virtual auto insert_tuple(std::span<const std::byte> tuple) -> Result<TupleId> = 0;
    virtual auto delete_tuple(TupleId id) -> VoidResult = 0;
    virtual auto update_tuple(TupleId id, std::span<const std::byte> tuple) -> Result<TupleId> = 0;

    virtual auto begin_scan() -> Result<ScanHandle> = 0;
    virtual auto scan_next(ScanHandle& handle) -> Result<std::optional<Tuple>> = 0;
    virtual auto end_scan(ScanHandle& handle) -> VoidResult = 0;

    [[nodiscard]] virtual auto page_size() const -> usize = 0;
};

}  // namespace edb
