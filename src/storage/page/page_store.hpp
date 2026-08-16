#pragma once

// src/storage/page/page_store.hpp
//
// EdbPageStore maps logical page IDs to byte offsets on top of an
// EdbStorageIOOps backend. It performs no caching and has no knowledge of page
// contents. Buffer pools and storage engines build on this layer.

#include <cstddef>
#include <span>

#include "storage/io/io_ops.hpp"
#include "utils/assert.hpp"
#include "utils/error.hpp"
#include "utils/primitives.hpp"

namespace edb {

struct PageStoreConfig {
    usize page_size{8192};
};

class PageStore {
public:
    PageStore() = default;

    PageStore(const PageStore&) = delete;
    PageStore& operator=(const PageStore&) = delete;
    PageStore(PageStore&&) = delete;
    PageStore& operator=(PageStore&&) = delete;

    ~PageStore() = default;

    auto open(StorageIO& backend, const PageStoreConfig& cfg) -> VoidResult;
    auto close() -> VoidResult;

    auto read_page(u64 page_id, std::span<std::byte> buf) -> VoidResult;
    auto write_page(u64 page_id, std::span<const std::byte> buf) -> VoidResult;

    auto allocate_page() -> Result<u64>;
    auto page_count() -> Result<u64>;
    [[nodiscard]] auto page_size() const -> usize;
    auto sync() -> VoidResult;

private:
    [[nodiscard]] auto check_open() const -> VoidResult;
    [[nodiscard]] auto byte_size_for_page_count(u64 count) const -> Result<u64>;
    [[nodiscard]] auto byte_offset_for_page(u64 page_id) const -> Result<u64>;
    [[nodiscard]] auto validate_existing_page(u64 page_id) -> VoidResult;

    StorageIO* io{nullptr};
    PageStoreConfig config{};
};

}  // namespace edb
