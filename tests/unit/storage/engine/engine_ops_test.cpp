// tests/unit/storage/engine/engine_ops_test.cpp
//
// Tests for the tuple-level storage engine interface. The mock implementation
// verifies that public virtual methods dispatch to concrete overrides.

#include <gtest/gtest.h>

#include <cstddef>
#include <optional>
#include <span>
#include <vector>

#include "storage/engine/engine.hpp"

using namespace edb;

class MockEngineOps : public StorageEngine {
public:
    usize configured_page_size{0};
    usize open_calls{0};
    usize close_calls{0};
    usize insert_calls{0};
    usize delete_calls{0};
    usize update_calls{0};
    usize begin_scan_calls{0};
    usize scan_next_calls{0};
    usize end_scan_calls{0};
    std::vector<std::byte> last_tuple;

    auto open(PageStore& store, const EngineConfig& cfg) -> VoidResult override {
        ++open_calls;
        configured_page_size = cfg.page_size;
        EXPECT_EQ(store.page_size().value, cfg.page_size.value);
        return {};
    }

    auto close() -> VoidResult override {
        ++close_calls;
        return {};
    }

    auto insert_tuple(std::span<const std::byte> tuple) -> Result<TupleId> override {
        ++insert_calls;
        last_tuple.assign(tuple.begin(), tuple.end());
        return TupleId{.page_id = u64{7}, .slot_idx = u16{3}};
    }

    auto delete_tuple(TupleId id) -> VoidResult override {
        ++delete_calls;
        EXPECT_EQ(id.page_id.value, u64{7}.value);
        EXPECT_EQ(id.slot_idx.value, u16{3}.value);
        return {};
    }

    auto update_tuple(TupleId id, std::span<const std::byte> tuple) -> Result<TupleId> override {
        ++update_calls;
        EXPECT_EQ(id.page_id.value, u64{7}.value);
        EXPECT_EQ(id.slot_idx.value, u16{3}.value);
        last_tuple.assign(tuple.begin(), tuple.end());
        return TupleId{.page_id = u64{8}, .slot_idx = u16{1}};
    }

    auto begin_scan() -> Result<ScanHandle> override {
        ++begin_scan_calls;
        return ScanHandle{.value = u64{42}};
    }

    auto scan_next(ScanHandle& handle) -> Result<std::optional<Tuple>> override {
        ++scan_next_calls;
        EXPECT_EQ(handle.value.value, u64{42}.value);
        handle.value = u64{43};
        return Tuple{.id = TupleId{.page_id = u64{1}, .slot_idx = u16{2}},
                     .data = {std::byte{0xAA}, std::byte{0xBB}}};
    }

    auto end_scan(ScanHandle& handle) -> VoidResult override {
        ++end_scan_calls;
        EXPECT_EQ(handle.value.value, u64{43}.value);
        return {};
    }

    [[nodiscard]] auto page_size() const -> usize override { return configured_page_size; }
};

class MockPageIO : public StorageIO {
private:
    auto open_impl(const char* /*path*/, const IOConfig& /*cfg*/) -> VoidResult override {
        return {};
    }
    auto close_impl() -> VoidResult override { return {}; }
    auto read_impl(u64 /*offset*/, std::span<std::byte> /*buf*/) -> Result<usize> override {
        return usize{0};
    }
    auto write_impl(u64 /*offset*/, std::span<const std::byte> buf) -> Result<usize> override {
        return usize{buf.size()};
    }
    auto sync_impl() -> VoidResult override { return {}; }
    auto datasync_impl() -> VoidResult override { return {}; }
    auto truncate_impl(u64 /*size*/) -> VoidResult override { return {}; }
    auto file_size_impl() -> Result<u64> override { return u64{0}; }
};

TEST(EdbEngineConfig, DefaultPageSize) {
    constexpr EngineConfig cfg{};
    EXPECT_EQ(cfg.page_size.value, usize{8192}.value);
}

TEST(EdbTupleId, FieldTypes) {
    static_assert(std::is_same_v<decltype(TupleId::page_id), u64>);
    static_assert(std::is_same_v<decltype(TupleId::slot_idx), u16>);
}

TEST(EdbStorageEngineOps, LifecycleDispatchesToOverride) {
    MockPageIO io;
    PageStore page_store;
    MockEngineOps engine;

    ASSERT_TRUE(page_store.open(io, PageStoreConfig{.page_size = usize{4096}}).has_value());
    ASSERT_TRUE(engine.open(page_store, EngineConfig{.page_size = usize{4096}}).has_value());
    EXPECT_EQ(engine.open_calls.value, usize{1}.value);
    EXPECT_EQ(engine.page_size().value, usize{4096}.value);

    ASSERT_TRUE(engine.close().has_value());
    EXPECT_EQ(engine.close_calls.value, usize{1}.value);
}

TEST(EdbStorageEngineOps, DmlDispatchesToOverride) {
    MockEngineOps engine;
    const std::vector<std::byte> tuple{std::byte{0x01}, std::byte{0x02}};

    auto inserted = engine.insert_tuple(tuple);
    ASSERT_TRUE(inserted.has_value());
    EXPECT_EQ(inserted->page_id.value, u64{7}.value);
    EXPECT_EQ(inserted->slot_idx.value, u16{3}.value);
    EXPECT_EQ(engine.insert_calls.value, usize{1}.value);
    EXPECT_EQ(engine.last_tuple, tuple);

    ASSERT_TRUE(engine.delete_tuple(*inserted).has_value());
    EXPECT_EQ(engine.delete_calls.value, usize{1}.value);

    auto updated = engine.update_tuple(*inserted, tuple);
    ASSERT_TRUE(updated.has_value());
    EXPECT_EQ(updated->page_id.value, u64{8}.value);
    EXPECT_EQ(updated->slot_idx.value, u16{1}.value);
    EXPECT_EQ(engine.update_calls.value, usize{1}.value);
}

TEST(EdbStorageEngineOps, BeginScanDispatchesToOverride) {
    MockEngineOps engine;

    auto handle = engine.begin_scan();
    ASSERT_TRUE(handle.has_value());
    EXPECT_EQ(handle->value.value, u64{42}.value);
    EXPECT_EQ(engine.begin_scan_calls.value, usize{1}.value);
}

TEST(EdbStorageEngineOps, ScanNextDispatchesToOverride) {
    MockEngineOps engine;
    auto handle = ScanHandle{.value = u64{42}};

    auto tuple = engine.scan_next(handle);
    ASSERT_TRUE(tuple.has_value());
    if (!tuple->has_value()) {
        FAIL() << "expected scan_next to return one tuple";
    }
    const auto& scanned = tuple->value();
    EXPECT_EQ(scanned.id.page_id.value, u64{1}.value);
    EXPECT_EQ(scanned.id.slot_idx.value, u16{2}.value);
    EXPECT_EQ(scanned.data.size(), std::size_t{2});
    EXPECT_EQ(engine.scan_next_calls.value, usize{1}.value);
}

TEST(EdbStorageEngineOps, EndScanDispatchesToOverride) {
    MockEngineOps engine;
    auto handle = ScanHandle{.value = u64{43}};

    ASSERT_TRUE(engine.end_scan(handle).has_value());
    EXPECT_EQ(engine.end_scan_calls.value, usize{1}.value);
}
