// src/storage/page/page_store.cpp

#include "storage/page/page_store.hpp"

#include <cstdint>
#include <expected>
#include <limits>

namespace edb {

auto EdbPageStore::open(EdbStorageIOOps& backend, const EdbPageStoreConfig& cfg) -> EdbStatus {
    io = &backend;
    config = cfg;
    return {};
}

auto EdbPageStore::close() -> EdbStatus {
    io = nullptr;
    return {};
}

auto EdbPageStore::read_page(u64 page_id, std::span<std::byte> buf) -> EdbStatus {
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
        return std::unexpected(EdbError::PageNotFound);
    }
    return {};
}

auto EdbPageStore::write_page(u64 page_id, std::span<const std::byte> buf) -> EdbStatus {
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
        return std::unexpected(EdbError::IoError);
    }
    return {};
}

auto EdbPageStore::allocate_page() -> EdbResult<u64> {
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

auto EdbPageStore::page_count() -> EdbResult<u64> {
    if (auto status = check_open(); !status) {
        return std::unexpected(status.error());
    }

    auto size_result = io->file_size();
    if (!size_result) {
        return std::unexpected(size_result.error());
    }

    const auto page_size_bytes = static_cast<std::uint64_t>(config.page_size.value);
    if ((size_result->value % page_size_bytes) != 0U) {
        return std::unexpected(EdbError::Corruption);
    }
    return u64{size_result->value / page_size_bytes};
}

auto EdbPageStore::page_size() const -> usize {
    return config.page_size;
}

auto EdbPageStore::sync() -> EdbStatus {
    if (auto status = check_open(); !status) {
        return status;
    }
    return io->sync();
}

auto EdbPageStore::check_open() const -> EdbStatus {
    if (io == nullptr) {
        return std::unexpected(EdbError::IoError);
    }
    return {};
}

auto EdbPageStore::byte_size_for_page_count(u64 count) const -> EdbResult<u64> {
    const auto page_size_bytes = static_cast<std::uint64_t>(config.page_size.value);
    if (count.value > (std::numeric_limits<std::uint64_t>::max() / page_size_bytes)) {
        return std::unexpected(EdbError::Overflow);
    }
    return u64{count.value * page_size_bytes};
}

auto EdbPageStore::byte_offset_for_page(u64 page_id) const -> EdbResult<u64> {
    return byte_size_for_page_count(page_id);
}

auto EdbPageStore::validate_existing_page(u64 page_id) -> EdbStatus {
    if (auto status = check_open(); !status) {
        return status;
    }

    auto count = page_count();
    if (!count) {
        return std::unexpected(count.error());
    }
    if (page_id >= *count) {
        return std::unexpected(EdbError::PageNotFound);
    }
    return {};
}

}  // namespace edb
