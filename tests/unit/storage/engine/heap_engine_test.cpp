// tests/unit/storage/engine/heap_engine_test.cpp

#include "storage/engine/heap/heap_engine.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <map>
#include <span>
#include <vector>

#include "storage/engine/heap/page.hpp"
#include "transaction/visibility.hpp"

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

class FakeStatusReader final : public TransactionStatusReader {
   public:
    auto set(TxId id, TxStatus status) -> void { statuses[id] = status; }

    [[nodiscard]] auto status(TxId id) const -> Result<TxStatus> override {
        const auto found = statuses.find(id);
        if (found == statuses.end()) {
            return std::unexpected(Error::NotFound);
        }
        return found->second;
    }

   private:
    std::map<TxId, TxStatus> statuses;
};

[[nodiscard]] auto make_snapshot(TxId xmax, std::vector<TxId> active = {}) -> Snapshot {
    auto xmin = xmax;
    for (const auto id : active) {
        if (id.value < xmin.value) {
            xmin = id;
        }
    }
    return Snapshot{.xmin = xmin, .xmax = xmax, .active = std::move(active)};
}

[[nodiscard]] auto make_tx(TxId id, std::vector<TxId> active = {}) -> Transaction {
    return Transaction{.id = id, .snapshot = make_snapshot(id, std::move(active))};
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

    [[nodiscard]] static auto collect_scan(EdbHeapEngine& engine, const VisibilityContext& context,
                                           const TransactionStatusReader& statuses)
        -> Result<std::vector<std::vector<std::byte>>> {
        auto handle = engine.begin_scan(context, statuses);
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

TEST_F(HeapEngineTest, TransactionalInsertIsVisibleToOwningTransaction) {
    EdbHeapEngine engine;
    open_engine(engine);
    FakeStatusReader statuses;
    const auto tx = make_tx(TxId{u64{1}});
    const auto payload = make_tuple({0x31U});

    ASSERT_TRUE(engine.insert(tx, payload).has_value());

    auto tuples = collect_scan(
        engine, VisibilityContext{.snapshot = tx.snapshot, .current_tx = tx.id}, statuses);
    ASSERT_TRUE(tuples.has_value());
    ASSERT_EQ(tuples->size(), std::size_t{1});
    EXPECT_EQ((*tuples)[0], payload);
}

TEST_F(HeapEngineTest, TransactionalScanHidesOtherInProgressInsert) {
    EdbHeapEngine engine;
    open_engine(engine);
    FakeStatusReader statuses;
    statuses.set(TxId{u64{1}}, TxStatus::InProgress);

    const auto writer = make_tx(TxId{u64{1}});
    const auto reader =
        Transaction{.id = TxId{u64{2}}, .snapshot = make_snapshot(TxId{u64{3}}, {writer.id})};
    ASSERT_TRUE(engine.insert(writer, make_tuple({0x41U})).has_value());

    auto tuples = collect_scan(
        engine, VisibilityContext{.snapshot = reader.snapshot, .current_tx = reader.id}, statuses);
    ASSERT_TRUE(tuples.has_value());
    EXPECT_TRUE(tuples->empty());
}

TEST_F(HeapEngineTest, TransactionalScanShowsCommittedInsertToLaterSnapshot) {
    EdbHeapEngine engine;
    open_engine(engine);
    FakeStatusReader statuses;
    statuses.set(TxId{u64{1}}, TxStatus::Committed);
    const auto writer = make_tx(TxId{u64{1}});
    const auto reader = Transaction{.id = TxId{u64{2}}, .snapshot = make_snapshot(TxId{u64{3}})};
    const auto payload = make_tuple({0x51U});

    ASSERT_TRUE(engine.insert(writer, payload).has_value());

    auto tuples = collect_scan(
        engine, VisibilityContext{.snapshot = reader.snapshot, .current_tx = reader.id}, statuses);
    ASSERT_TRUE(tuples.has_value());
    ASSERT_EQ(tuples->size(), std::size_t{1});
    EXPECT_EQ((*tuples)[0], payload);
}

TEST_F(HeapEngineTest, TransactionalDeleteUsesXmaxVisibility) {
    EdbHeapEngine engine;
    open_engine(engine);
    FakeStatusReader statuses;
    statuses.set(TxId{u64{1}}, TxStatus::Committed);
    statuses.set(TxId{u64{2}}, TxStatus::InProgress);

    const auto inserter = make_tx(TxId{u64{1}});
    const auto deleter = make_tx(TxId{u64{2}});
    const auto payload = make_tuple({0x61U});
    auto id = engine.insert(inserter, payload);
    ASSERT_TRUE(id.has_value());
    ASSERT_TRUE(engine.delete_tuple(deleter, *id).has_value());

    auto own_scan = collect_scan(
        engine, VisibilityContext{.snapshot = deleter.snapshot, .current_tx = deleter.id},
        statuses);
    ASSERT_TRUE(own_scan.has_value());
    EXPECT_TRUE(own_scan->empty());

    const auto concurrent_reader =
        Transaction{.id = TxId{u64{3}}, .snapshot = make_snapshot(TxId{u64{4}}, {deleter.id})};
    auto concurrent_scan = collect_scan(engine,
                                        VisibilityContext{.snapshot = concurrent_reader.snapshot,
                                                          .current_tx = concurrent_reader.id},
                                        statuses);
    ASSERT_TRUE(concurrent_scan.has_value());
    ASSERT_EQ(concurrent_scan->size(), std::size_t{1});
    EXPECT_EQ((*concurrent_scan)[0], payload);

    statuses.set(deleter.id, TxStatus::Committed);
    const auto later_reader =
        Transaction{.id = TxId{u64{4}}, .snapshot = make_snapshot(TxId{u64{5}})};
    auto later_scan = collect_scan(
        engine, VisibilityContext{.snapshot = later_reader.snapshot, .current_tx = later_reader.id},
        statuses);
    ASSERT_TRUE(later_scan.has_value());
    EXPECT_TRUE(later_scan->empty());
}

TEST_F(HeapEngineTest, TransactionalDeleteRejectsInProgressDeleteConflict) {
    EdbHeapEngine engine;
    open_engine(engine);
    FakeStatusReader statuses;
    statuses.set(TxId{u64{1}}, TxStatus::Committed);
    statuses.set(TxId{u64{2}}, TxStatus::InProgress);

    const auto inserter = make_tx(TxId{u64{1}});
    const auto first_deleter = make_tx(TxId{u64{2}});
    const auto second_deleter = make_tx(TxId{u64{3}});
    auto id = engine.insert(inserter, make_tuple({0x71U}));
    ASSERT_TRUE(id.has_value());
    ASSERT_TRUE(engine.delete_tuple(first_deleter, *id).has_value());

    auto conflict = engine.delete_tuple(second_deleter, *id, statuses);
    ASSERT_FALSE(conflict.has_value());
    EXPECT_EQ(conflict.error(), Error::TransactionAborted);
}

TEST_F(HeapEngineTest, TransactionalDeleteCanReplaceAbortedDeleteMarker) {
    EdbHeapEngine engine;
    open_engine(engine);
    FakeStatusReader statuses;
    statuses.set(TxId{u64{1}}, TxStatus::Committed);
    statuses.set(TxId{u64{2}}, TxStatus::Aborted);
    statuses.set(TxId{u64{3}}, TxStatus::Committed);

    const auto inserter = make_tx(TxId{u64{1}});
    const auto aborted_deleter = make_tx(TxId{u64{2}});
    const auto committed_deleter = make_tx(TxId{u64{3}});
    auto id = engine.insert(inserter, make_tuple({0x81U}));
    ASSERT_TRUE(id.has_value());
    ASSERT_TRUE(engine.delete_tuple(aborted_deleter, *id).has_value());
    ASSERT_TRUE(engine.delete_tuple(committed_deleter, *id, statuses).has_value());

    const auto reader = Transaction{.id = TxId{u64{4}}, .snapshot = make_snapshot(TxId{u64{5}})};
    auto tuples = collect_scan(
        engine, VisibilityContext{.snapshot = reader.snapshot, .current_tx = reader.id}, statuses);
    ASSERT_TRUE(tuples.has_value());
    EXPECT_TRUE(tuples->empty());
}

TEST_F(HeapEngineTest, TransactionalUpdateRejectsInProgressDeleteConflict) {
    EdbHeapEngine engine;
    open_engine(engine);
    FakeStatusReader statuses;
    statuses.set(TxId{u64{1}}, TxStatus::Committed);
    statuses.set(TxId{u64{2}}, TxStatus::InProgress);

    const auto inserter = make_tx(TxId{u64{1}});
    const auto deleter = make_tx(TxId{u64{2}});
    const auto updater = make_tx(TxId{u64{3}});
    auto id = engine.insert(inserter, make_tuple({0x91U}));
    ASSERT_TRUE(id.has_value());
    ASSERT_TRUE(engine.delete_tuple(deleter, *id).has_value());

    auto conflict = engine.update_tuple(updater, *id, make_tuple({0x92U}), statuses);
    ASSERT_FALSE(conflict.has_value());
    EXPECT_EQ(conflict.error(), Error::TransactionAborted);
}
