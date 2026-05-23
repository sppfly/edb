// src/storage/page/page_store.cpp

#include "storage/page/page_store.hpp"

#include <cstdint>
#include <expected>
#include <limits>

namespace edb {

auto PageStore::open(StorageIOOps& backend, const PageStoreConfig& cfg) -> VoidResult {
    io = &backend;
    config = cfg;
    return {};
}

auto PageStore::close() -> VoidResult {
    io = nullptr;
    return {};
}

auto PageStore::read_page(u64 page_id, std::span<std::byte> buf) -> VoidResult {
    if (auto status = validate_existing_page(page_id); !status) {
        return status;
    }

    auto offset = byte_offset_for_page(page_id);
    if (!offset) {
        return std::unexpected(offset.error());
    }

    auto page_buf = buf.subspan(0, config.page_size.value);
    auto read_result = io->read(*offset, page_buf);
    if (!read_result) {
        return std::unexpected(read_result.error());
    }
    if (*read_result != config.page_size) {
        return std::unexpected(Error::PageNotFound);
    }
    return {};
}

auto PageStore::write_page(u64 page_id, std::span<const std::byte> buf) -> VoidResult {
    if (auto status = validate_existing_page(page_id); !status) {
        return status;
    }

    auto offset = byte_offset_for_page(page_id);
    if (!offset) {
        return std::unexpected(offset.error());
    }

    auto page_buf = buf.subspan(0, config.page_size.value);
    auto write_result = io->write(*offset, page_buf);
    if (!write_result) {
        return std::unexpected(write_result.error());
    }
    if (*write_result != config.page_size) {
        return std::unexpected(Error::IoError);
    }
    return {};
}

auto PageStore::allocate_page() -> Result<u64> {
    if (auto status = check_open(); !status) {
        return std::unexpected(status.error());
    }

    auto current_count = page_count();
    if (!current_count) {
        return std::unexpected(current_count.error());
    }

    auto new_count = u64{current_count->value + u64{1}.value};
    auto new_size = byte_size_for_page_count(new_count);
    if (!new_size) {
        return std::unexpected(new_size.error());
    }

    auto truncate_result = io->truncate(*new_size);
    if (!truncate_result) {
        return std::unexpected(truncate_result.error());
    }
    return *current_count;
}

auto PageStore::page_count() -> Result<u64> {
    if (auto status = check_open(); !status) {
        return std::unexpected(status.error());
    }

    auto size_result = io->file_size();
    if (!size_result) {
        return std::unexpected(size_result.error());
    }

    const auto page_size_bytes = static_cast<std::uint64_t>(config.page_size.value);
    if ((size_result->value % page_size_bytes) != 0U) {
        return std::unexpected(Error::Corruption);
    }
    return u64{size_result->value / page_size_bytes};
}

auto PageStore::page_size() const -> usize {
    return config.page_size;
}

auto PageStore::sync() -> VoidResult {
    if (auto status = check_open(); !status) {
        return status;
    }
    return io->sync();
}

auto PageStore::check_open() const -> VoidResult {
    if (io == nullptr) {
        return std::unexpected(Error::IoError);
    }
    return {};
}

auto PageStore::byte_size_for_page_count(u64 count) const -> Result<u64> {
    const auto page_size_bytes = static_cast<std::uint64_t>(config.page_size.value);
    if (count.value > (std::numeric_limits<std::uint64_t>::max() / page_size_bytes)) {
        return std::unexpected(Error::Overflow);
    }
    return u64{count.value * page_size_bytes};
}

auto PageStore::byte_offset_for_page(u64 page_id) const -> Result<u64> {
    return byte_size_for_page_count(page_id);
}

auto PageStore::validate_existing_page(u64 page_id) -> VoidResult {
    if (auto status = check_open(); !status) {
        return status;
    }

    auto count = page_count();
    if (!count) {
        return std::unexpected(count.error());
    }
    if (page_id >= *count) {
        return std::unexpected(Error::PageNotFound);
    }
    return {};
}

}  // namespace edb
