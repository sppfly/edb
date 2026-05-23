#pragma once

// src/storage/page/page_store.hpp
//
// EdbPageStore maps logical page IDs to byte offsets on top of an
// EdbStorageIOOps backend. It performs no caching and has no knowledge of page
// contents. Buffer pools and storage engines build on this layer.

#include <cstddef>
#include <span>

#include "storage/io/io_ops.hpp"
#include "utils/contracts.hpp"
#include "utils/error.hpp"
#include "utils/primitives.hpp"

namespace edb {

struct EdbPageStoreConfig {
    usize page_size{8192};
};

class EdbPageStore {
   public:
    EdbPageStore() = default;

    EdbPageStore(const EdbPageStore&) = delete;
    EdbPageStore& operator=(const EdbPageStore&) = delete;
    EdbPageStore(EdbPageStore&&) = delete;
    EdbPageStore& operator=(EdbPageStore&&) = delete;

    ~EdbPageStore() = default;

    auto open(EdbStorageIOOps& backend, const EdbPageStoreConfig& cfg) -> EdbStatus
        EDB_PRE(cfg.page_size > usize{0});
    auto close() -> EdbStatus;

    auto read_page(u64 page_id, std::span<std::byte> buf) -> EdbStatus
        EDB_PRE(buf.size() >= page_size().value);
    auto write_page(u64 page_id, std::span<const std::byte> buf) -> EdbStatus
        EDB_PRE(buf.size() >= page_size().value);

    auto allocate_page() -> EdbResult<u64>;
    auto page_count() -> EdbResult<u64>;
    [[nodiscard]] auto page_size() const -> usize;
    auto sync() -> EdbStatus;

   private:
    [[nodiscard]] auto check_open() const -> EdbStatus;
    [[nodiscard]] auto byte_size_for_page_count(u64 count) const -> EdbResult<u64>;
    [[nodiscard]] auto byte_offset_for_page(u64 page_id) const -> EdbResult<u64>;
    [[nodiscard]] auto validate_existing_page(u64 page_id) -> EdbStatus;

    EdbStorageIOOps* io{nullptr};
    EdbPageStoreConfig config{};
};

}  // namespace edb
