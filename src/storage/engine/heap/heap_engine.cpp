// src/storage/engine/heap/heap_engine.cpp

#include "storage/engine/heap/heap_engine.hpp"

#include <cstdint>
#include <expected>
#include <optional>

#include "storage/engine/heap/page.hpp"

namespace edb {

namespace {

constexpr auto CURSOR_SLOT_BASE = std::uint64_t{65536};
constexpr auto CURSOR_PAGE_MASK = std::uint64_t{0xFFFFFFFFULL};

}  // namespace

auto EdbHeapEngine::open_impl(PageStore& store, const EngineConfig& cfg) -> VoidResult {
    std::scoped_lock guard{latch};
    page_store = &store;
    config = cfg;
    auto status = buffer_pool.open(store, BufferPoolConfig{.capacity_pages = cfg.buffer_pool_pages,
                                                           .eviction = cfg.buffer_eviction});
    if (!status) {
        page_store = nullptr;
        return status;
    }
    opened = b8{true};
    return {};
}

auto EdbHeapEngine::close_impl() -> VoidResult {
    std::scoped_lock guard{latch};
    if (!opened.value) {
        return {};
    }
    auto status = buffer_pool.close();
    page_store = nullptr;
    opened = b8{false};
    return status;
}

auto EdbHeapEngine::insert_impl(std::span<const std::byte> tuple) -> Result<TupleId> {
    std::scoped_lock guard{latch};
    if (auto status = check_open(); !status) {
        return std::unexpected(status.error());
    }

    auto count = page_store->page_count();
    if (!count) {
        return std::unexpected(count.error());
    }

    for (u64 page_id{0}; page_id < *count; ++page_id) {
        auto inserted = insert_into_existing_page(page_id, tuple);
        if (!inserted) {
            return std::unexpected(inserted.error());
        }
        if (inserted->has_value()) {
            return **inserted;
        }
    }
    return insert_into_new_page(tuple);
}

auto EdbHeapEngine::delete_tuple_impl(TupleId id) -> VoidResult {
    std::scoped_lock guard{latch};
    if (auto status = check_open(); !status) {
        return status;
    }

    auto handle = buffer_pool.fetch(id.page_id);
    if (!handle) {
        return std::unexpected(handle.error());
    }
    auto status = heap::delete_tuple(handle->data(), id.slot_idx);
    if (!status) {
        auto unpinned = unpin_clean(*handle);
        if (!unpinned) {
            return unpinned;
        }
        return status;
    }
    return buffer_pool.unpin(*handle, b8{true});
}

auto EdbHeapEngine::update_tuple_impl(TupleId id, std::span<const std::byte> tuple)
    -> Result<TupleId> {
    auto delete_status = delete_tuple_impl(id);
    if (!delete_status) {
        return std::unexpected(delete_status.error());
    }
    return insert_impl(tuple);
}

auto EdbHeapEngine::begin_scan_impl() -> Result<ScanHandle> {
    std::scoped_lock guard{latch};
    if (auto status = check_open(); !status) {
        return std::unexpected(status.error());
    }
    return ScanHandle{.value = encode_cursor(u64{0}, u16{0})};
}

auto EdbHeapEngine::scan_next_impl(ScanHandle& handle) -> Result<std::optional<Tuple>> {
    std::scoped_lock guard{latch};
    if (auto status = check_open(); !status) {
        return std::unexpected(status.error());
    }

    auto count = page_store->page_count();
    if (!count) {
        return std::unexpected(count.error());
    }

    for (auto page_id = cursor_page_id(handle); page_id < *count; ++page_id) {
        auto frame = buffer_pool.fetch(page_id);
        if (!frame) {
            return std::unexpected(frame.error());
        }

        auto slots = heap::slot_count(frame->data());
        if (!slots) {
            if (auto status = unpin_clean(*frame); !status) {
                return std::unexpected(status.error());
            }
            return std::unexpected(slots.error());
        }

        auto slot_idx = page_id == cursor_page_id(handle) ? cursor_slot_idx(handle) : u16{0};
        for (; slot_idx < *slots; ++slot_idx) {
            auto tuple = scan_slot(*frame, handle, page_id, slot_idx);
            if (!tuple) {
                return std::unexpected(tuple.error());
            }
            if (tuple->has_value()) {
                return std::move(*tuple);
            }
        }

        auto status = unpin_clean(*frame);
        if (!status) {
            return std::unexpected(status.error());
        }
        handle.value = encode_cursor(page_id + u64{1}, u16{0});
    }
    return std::optional<Tuple>{};
}

auto EdbHeapEngine::end_scan_impl(ScanHandle& handle) -> VoidResult {
    std::scoped_lock guard{latch};
    handle.value = encode_cursor(u64{0}, u16{0});
    return {};
}

auto EdbHeapEngine::page_size_impl() const -> usize {
    return config.page_size;
}

auto EdbHeapEngine::check_open() const -> VoidResult {
    if (!opened.value || page_store == nullptr) {
        return std::unexpected(Error::IoError);
    }
    return {};
}

auto EdbHeapEngine::insert_into_existing_page(u64 page_id, std::span<const std::byte> tuple)
    -> Result<std::optional<TupleId>> {
    auto frame = buffer_pool.fetch(page_id);
    if (!frame) {
        return std::unexpected(frame.error());
    }

    auto can_fit = heap::can_insert(frame->data(), usize{tuple.size()});
    if (!can_fit) {
        if (auto status = unpin_clean(*frame); !status) {
            return std::unexpected(status.error());
        }
        return std::unexpected(can_fit.error());
    }
    if (!can_fit->value) {
        auto status = buffer_pool.unpin(*frame, b8{false});
        if (!status) {
            return std::unexpected(status.error());
        }
        return std::optional<TupleId>{};
    }

    auto slot = heap::insert_tuple(frame->data(), tuple);
    if (!slot) {
        if (auto status = unpin_clean(*frame); !status) {
            return std::unexpected(status.error());
        }
        return std::unexpected(slot.error());
    }

    auto status = buffer_pool.unpin(*frame, b8{true});
    if (!status) {
        return std::unexpected(status.error());
    }
    return TupleId{.page_id = page_id, .slot_idx = *slot};
}

auto EdbHeapEngine::insert_into_new_page(std::span<const std::byte> tuple) -> Result<TupleId> {
    // Fail before extending the file when the tuple cannot fit an empty page.
    const auto page_capacity = usize{config.page_size.value - heap::PAGE_HEADER_SIZE.value};
    if ((tuple.size() + heap::SLOT_SIZE.value) > page_capacity.value) {
        return std::unexpected(Error::OutOfMemory);
    }

    auto allocated = page_store->allocate_page();
    if (!allocated) {
        return std::unexpected(allocated.error());
    }

    auto frame = buffer_pool.fetch_new(*allocated);
    if (!frame) {
        return std::unexpected(frame.error());
    }
    if (auto status = heap::initialize_page(frame->data(), *allocated); !status) {
        if (auto unpinned = unpin_clean(*frame); !unpinned) {
            return std::unexpected(unpinned.error());
        }
        return std::unexpected(status.error());
    }
    auto slot = heap::insert_tuple(frame->data(), tuple);
    if (!slot) {
        if (auto status = unpin_clean(*frame); !status) {
            return std::unexpected(status.error());
        }
        return std::unexpected(slot.error());
    }

    auto status = buffer_pool.unpin(*frame, b8{true});
    if (!status) {
        return std::unexpected(status.error());
    }
    return TupleId{.page_id = *allocated, .slot_idx = *slot};
}

auto EdbHeapEngine::scan_slot(FrameHandle& frame, ScanHandle& handle, u64 page_id, u16 slot_idx)
    -> Result<std::optional<Tuple>> {
    auto live = heap::is_live_slot(frame.data(), slot_idx);
    if (!live) {
        if (auto status = unpin_clean(frame); !status) {
            return std::unexpected(status.error());
        }
        return std::unexpected(live.error());
    }
    if (!live->value) {
        return std::optional<Tuple>{};
    }

    auto tuple = heap::read_tuple(frame.data(), slot_idx);
    if (!tuple) {
        if (auto status = unpin_clean(frame); !status) {
            return std::unexpected(status.error());
        }
        return std::unexpected(tuple.error());
    }

    handle.value = encode_cursor(page_id, u16{static_cast<std::uint16_t>(slot_idx.value + 1U)});
    auto data = std::move(*tuple);
    auto status = unpin_clean(frame);
    if (!status) {
        return std::unexpected(status.error());
    }
    return Tuple{.id = TupleId{.page_id = page_id, .slot_idx = slot_idx}, .data = std::move(data)};
}

auto EdbHeapEngine::unpin_clean(FrameHandle& handle) -> VoidResult {
    return buffer_pool.unpin(handle, b8{false});
}

auto EdbHeapEngine::encode_cursor(u64 page_id, u16 slot_idx) -> u64 {
    return u64{((page_id.value & CURSOR_PAGE_MASK) * CURSOR_SLOT_BASE) + slot_idx.value};
}

auto EdbHeapEngine::cursor_page_id(ScanHandle handle) -> u64 {
    return u64{(handle.value.value / CURSOR_SLOT_BASE) & CURSOR_PAGE_MASK};
}

auto EdbHeapEngine::cursor_slot_idx(ScanHandle handle) -> u16 {
    return u16{static_cast<std::uint16_t>(handle.value.value % CURSOR_SLOT_BASE)};
}

}  // namespace edb
