// tests/unit/storage/engine/heap_engine_test.cpp

#include "storage/engine/heap/heap_engine.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <span>
#include <vector>

#include "storage/engine/heap/page.hpp"

using namespace edb;

namespace {

[[nodiscard]] auto make_tuple(std::initializer_list<unsigned int> values)
    -> std::vector<std::byte> {
    std::vector<std::byte> result;
    result.reserve(values.size());
    for (const auto value : values) {
        result.push_back(std::byte{static_cast<unsigned char>(value)});
    }
    return result;
}

}  // namespace

class MockHeapIOOps : public StorageIOOps {
   public:
    std::vector<std::byte> storage;

   private:
    auto open_impl(const char* /*path*/, const IOConfig& /*cfg*/) -> VoidResult override {
        return {};
    }
    auto close_impl() -> VoidResult override { return {}; }

    auto read_impl(u64 offset, std::span<std::byte> buf) -> Result<usize> override {
        const auto off = offset.value;
        if (off >= storage.size()) {
            return usize{0};
        }
        const auto available = storage.size() - off;
        const auto count = std::min(available, buf.size());
        auto src = std::span<const std::byte>{storage}.subspan(off, count);
        std::ranges::copy(src, buf.begin());
        return usize{count};
    }

    auto write_impl(u64 offset, std::span<const std::byte> buf) -> Result<usize> override {
        const auto off = offset.value;
        if ((off + buf.size()) > storage.size()) {
            storage.resize(off + buf.size());
        }
        auto dst = std::span<std::byte>{storage}.subspan(off, buf.size());
        std::ranges::copy(buf, dst.begin());
        return usize{buf.size()};
    }

    auto sync_impl() -> VoidResult override { return {}; }
    auto datasync_impl() -> VoidResult override { return {}; }
    auto truncate_impl(u64 size) -> VoidResult override {
        storage.resize(size.value);
        return {};
    }
    auto file_size_impl() -> Result<u64> override { return u64{storage.size()}; }
};

class HeapEngineTest : public ::testing::Test {
   protected:
    void SetUp() override {
        ASSERT_TRUE(page_store.open(io, PageStoreConfig{.page_size = usize{256}}).has_value());
    }

    auto open_engine(EdbHeapEngine& engine) -> void {
        ASSERT_TRUE(engine
                        .open(page_store,
                              EngineConfig{.page_size = usize{256}, .buffer_pool_pages = usize{2}})
                        .has_value());
    }

    [[nodiscard]] static auto collect_scan(EdbHeapEngine& engine)
        -> Result<std::vector<std::vector<std::byte>>> {
        auto handle = engine.begin_scan();
        if (!handle) {
            return std::unexpected(handle.error());
        }

        std::vector<std::vector<std::byte>> tuples;
        while (true) {
            auto next = engine.scan_next(*handle);
            if (!next) {
                return std::unexpected(next.error());
            }
            if (!next->has_value()) {
                break;
            }
            tuples.push_back((*next)->data);
        }
        auto status = engine.end_scan(*handle);
        if (!status) {
            return std::unexpected(status.error());
        }
        return tuples;
    }

   public:
    MockHeapIOOps io;
    PageStore page_store;
};

TEST(HeapPage, InsertReadDeleteTuple) {
    std::vector<std::byte> page(256);
    ASSERT_TRUE(heap::initialize_page(page, u64{0}).has_value());
    const auto payload = make_tuple({0x01U, 0x02U, 0x03U});

    auto slot = heap::insert_tuple(page, payload);
    ASSERT_TRUE(slot.has_value());
    EXPECT_EQ(slot->value, u16{0}.value);

    auto read = heap::read_tuple(page, *slot);
    ASSERT_TRUE(read.has_value());
    EXPECT_EQ(*read, payload);

    ASSERT_TRUE(heap::delete_tuple(page, *slot).has_value());
    auto live = heap::is_live_slot(page, *slot);
    ASSERT_TRUE(live.has_value());
    EXPECT_FALSE(live->value);
}

TEST_F(HeapEngineTest, InsertAndScanTuples) {
    EdbHeapEngine engine;
    open_engine(engine);
    const auto first = make_tuple({0x10U});
    const auto second = make_tuple({0x20U, 0x21U});

    ASSERT_TRUE(engine.insert(first).has_value());
    ASSERT_TRUE(engine.insert(second).has_value());

    auto tuples = collect_scan(engine);
    ASSERT_TRUE(tuples.has_value());
    ASSERT_EQ(tuples->size(), std::size_t{2});
    EXPECT_EQ((*tuples)[0], first);
    EXPECT_EQ((*tuples)[1], second);
}

TEST_F(HeapEngineTest, DeleteSkipsTupleDuringScan) {
    EdbHeapEngine engine;
    open_engine(engine);
    const auto first = make_tuple({0xA0U});
    const auto second = make_tuple({0xB0U});

    auto first_id = engine.insert(first);
    ASSERT_TRUE(first_id.has_value());
    ASSERT_TRUE(engine.insert(second).has_value());
    ASSERT_TRUE(engine.delete_tuple(*first_id).has_value());

    auto tuples = collect_scan(engine);
    ASSERT_TRUE(tuples.has_value());
    ASSERT_EQ(tuples->size(), std::size_t{1});
    EXPECT_EQ((*tuples)[0], second);
}

TEST_F(HeapEngineTest, UpdateDeletesOldTupleAndInsertsNewTuple) {
    EdbHeapEngine engine;
    open_engine(engine);
    const auto old_tuple = make_tuple({0x01U});
    const auto new_tuple = make_tuple({0x02U, 0x03U});

    auto id = engine.insert(old_tuple);
    ASSERT_TRUE(id.has_value());
    ASSERT_TRUE(engine.update_tuple(*id, new_tuple).has_value());

    auto tuples = collect_scan(engine);
    ASSERT_TRUE(tuples.has_value());
    ASSERT_EQ(tuples->size(), std::size_t{1});
    EXPECT_EQ((*tuples)[0], new_tuple);
}

TEST_F(HeapEngineTest, ReopenScansPersistedTuples) {
    const auto first = make_tuple({0x11U, 0x12U});
    const auto second = make_tuple({0x21U, 0x22U});

    {
        EdbHeapEngine writer;
        open_engine(writer);
        ASSERT_TRUE(writer.insert(first).has_value());
        ASSERT_TRUE(writer.insert(second).has_value());
        ASSERT_TRUE(writer.close().has_value());
    }

    EdbHeapEngine reader;
    open_engine(reader);
    auto tuples = collect_scan(reader);
    ASSERT_TRUE(tuples.has_value());
    ASSERT_EQ(tuples->size(), std::size_t{2});
    EXPECT_EQ((*tuples)[0], first);
    EXPECT_EQ((*tuples)[1], second);
}

TEST_F(HeapEngineTest, PersistedSlotHoldsRawPayload) {
    EdbHeapEngine engine;
    open_engine(engine);
    const auto payload = make_tuple({0xAAU, 0xBBU, 0xCCU});

    auto inserted = engine.insert(payload);
    ASSERT_TRUE(inserted.has_value());
    ASSERT_TRUE(engine.close().has_value());

    std::vector<std::byte> page(usize{256}.value);
    ASSERT_TRUE(page_store.read_page(inserted->page_id, page).has_value());
    auto slot = heap::read_tuple(page, inserted->slot_idx);
    ASSERT_TRUE(slot.has_value());
    EXPECT_EQ(*slot, payload);
}

TEST_F(HeapEngineTest, OversizedInsertDoesNotGrowFile) {
    const auto valid = make_tuple({0x01U});
    const auto oversized = std::vector<std::byte>(300, std::byte{0xFF});

    {
        EdbHeapEngine writer;
        open_engine(writer);
        ASSERT_TRUE(writer.insert(valid).has_value());
        ASSERT_TRUE(writer.close().has_value());
    }

    auto count_before = page_store.page_count();
    ASSERT_TRUE(count_before.has_value());

    {
        EdbHeapEngine writer;
        open_engine(writer);
        auto inserted = writer.insert(oversized);
        ASSERT_FALSE(inserted.has_value());
        ASSERT_TRUE(writer.close().has_value());
    }

    auto count_after = page_store.page_count();
    ASSERT_TRUE(count_after.has_value());
    EXPECT_EQ(count_after->value, count_before->value);
}

TEST_F(HeapEngineTest, FailedUpdateLosesOldTuple) {
    EdbHeapEngine engine;
    open_engine(engine);
    const auto old_tuple = make_tuple({0x31U});
    const auto oversized = std::vector<std::byte>(300, std::byte{0xFF});

    auto id = engine.insert(old_tuple);
    ASSERT_TRUE(id.has_value());
    auto updated = engine.update_tuple(*id, oversized);
    ASSERT_FALSE(updated.has_value());

    auto tuples = collect_scan(engine);
    ASSERT_TRUE(tuples.has_value());
    EXPECT_TRUE(tuples->empty());
}
