// tests/unit/query/test_query_engine_wal.cpp
//
// Unit tests for the WAL-integrated QueryEngine constructor. Validates that:
//   1. A committed INSERT emits a TX_COMMIT WAL record.
//   2. A failed statement (parse error) emits a TX_ABORT WAL record.
//   3. An INSERT emits a HEAP_INSERT record before the TX_COMMIT record.
//   4. A committed INSERT survives crash+recovery via recover_heap.

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include "catalog/catalog.hpp"
#include "query/query_engine.hpp"
#include "storage/engine/heap/heap_engine.hpp"
#include "storage/io/io_ops.hpp"
#include "storage/page/page_store.hpp"
#include "transaction/visibility.hpp"
#include "types/builtin_types.hpp"
#include "wal/recovery.hpp"
#include "wal/wal_manager.hpp"

using namespace edb;

namespace {

// ---------------------------------------------------------------------------
// In-memory I/O backend (shared across query-layer tests)
// ---------------------------------------------------------------------------
class SharedMemoryIO final : public StorageIOOps {
   public:
    explicit SharedMemoryIO(std::shared_ptr<std::vector<std::byte>> bytes)
        : storage{std::move(bytes)} {}

   private:
    auto open_impl(const char* /*path*/, const IOConfig& /*cfg*/) -> VoidResult override {
        return {};
    }
    auto close_impl() -> VoidResult override { return {}; }

    auto read_impl(u64 offset, std::span<std::byte> buf) -> Result<usize> override {
        const auto off = offset.value;
        if (off >= storage->size()) {
            return usize{0};
        }
        const auto available = storage->size() - off;
        const auto count = std::min(available, buf.size());
        std::ranges::copy(std::span<const std::byte>{*storage}.subspan(off, count), buf.begin());
        return usize{count};
    }

    auto write_impl(u64 offset, std::span<const std::byte> buf) -> Result<usize> override {
        const auto off = offset.value;
        if ((off + buf.size()) > storage->size()) {
            storage->resize(off + buf.size());
        }
        std::ranges::copy(buf, std::span<std::byte>{*storage}.subspan(off, buf.size()).begin());
        return usize{buf.size()};
    }

    auto sync_impl() -> VoidResult override { return {}; }
    auto datasync_impl() -> VoidResult override { return {}; }
    auto truncate_impl(u64 size) -> VoidResult override {
        storage->resize(size.value);
        return {};
    }
    auto file_size_impl() -> Result<u64> override { return u64{storage->size()}; }

    std::shared_ptr<std::vector<std::byte>> storage;
};

// ---------------------------------------------------------------------------
// Relation backend factory that keeps a reference to each relation's byte
// buffer so tests can open a raw PageStore on the same bytes post-crash.
// ---------------------------------------------------------------------------
class MemoryRelationBackendFactory final : public RelationBackendFactory {
   public:
    auto open_backend(u32 relation_oid, std::string_view /*relation_name*/)
        -> Result<std::unique_ptr<StorageIOOps>> override {
        auto& bytes = relations[relation_oid];
        if (bytes == nullptr) {
            bytes = std::make_shared<std::vector<std::byte>>();
        }
        return std::make_unique<SharedMemoryIO>(bytes);
    }

    // Returns the underlying byte buffer for `relation_oid`, or nullptr if the
    // relation has never been opened.
    [[nodiscard]] auto bytes_for(u32 relation_oid) -> std::shared_ptr<std::vector<std::byte>> {
        auto it = relations.find(relation_oid);
        if (it == relations.end()) {
            return nullptr;
        }
        return it->second;
    }

   private:
    std::unordered_map<u32, std::shared_ptr<std::vector<std::byte>>> relations;
};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

[[nodiscard]] auto make_snapshot(TxId xmax) -> Snapshot {
    return Snapshot{.xmin = xmax, .xmax = xmax, .active = {}};
}

[[nodiscard]] auto collect_visible(EdbHeapEngine& engine, const TransactionStatusReader& statuses,
                                   TxId observer) -> Result<std::vector<std::vector<std::byte>>> {
    const auto context =
        VisibilityContext{.snapshot = make_snapshot(observer), .current_tx = observer};
    auto scan = engine.begin_scan(context, statuses);
    if (!scan) {
        return std::unexpected(scan.error());
    }
    std::vector<std::vector<std::byte>> tuples;
    while (true) {
        auto next = engine.scan_next(*scan);
        if (!next) {
            return std::unexpected(next.error());
        }
        if (!next->has_value()) {
            break;
        }
        tuples.push_back((*next)->data);
    }
    auto ended = engine.end_scan(*scan);
    if (!ended) {
        return std::unexpected(ended.error());
    }
    return tuples;
}

// ---------------------------------------------------------------------------
// Test fixture
// ---------------------------------------------------------------------------
class QueryEngineWalTest : public ::testing::Test {
   protected:
    void SetUp() override {
        ASSERT_TRUE(register_builtin_types(registry).has_value());
        ASSERT_TRUE(catalog.open().has_value());
        ASSERT_TRUE(wal.open().has_value());
    }

    [[nodiscard]] static auto default_engine_config() -> EngineConfig {
        return EngineConfig{.page_size = usize{512}, .buffer_pool_pages = usize{4}};
    }

    TypeRegistry registry;
    MemoryRelationBackendFactory factory;
    Catalog catalog{registry, factory, default_engine_config()};
    SharedMemoryIO wal_io{std::make_shared<std::vector<std::byte>>()};
    WalManager wal{wal_io};
};

}  // namespace

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

TEST_F(QueryEngineWalTest, CommitEmitsTxCommitRecord) {
    QueryEngine engine{catalog, registry, wal};

    ASSERT_TRUE(engine.execute("CREATE TABLE t (id INTEGER)").has_value())
        << engine.error_message();
    ASSERT_TRUE(engine.execute("INSERT INTO t VALUES (42)").has_value()) << engine.error_message();

    auto records = wal.read_all();
    ASSERT_TRUE(records.has_value());

    const auto has_commit = std::ranges::any_of(*records, [](const WalRecord& record) {
        return record.resource_manager == WalResourceManager::Transaction &&
               record.record_type == WalRecordType::Commit;
    });
    EXPECT_TRUE(has_commit) << "expected at least one TX_COMMIT WAL record after INSERT";
}

TEST_F(QueryEngineWalTest, AbortEmitsTxAbortRecord) {
    QueryEngine engine{catalog, registry, wal};

    // Reference a nonexistent table — fails during bind, after transaction begins.
    auto result = engine.execute("SELECT * FROM no_such_table");
    EXPECT_FALSE(result.has_value());

    auto records = wal.read_all();
    ASSERT_TRUE(records.has_value());

    const auto has_abort = std::ranges::any_of(*records, [](const WalRecord& record) {
        return record.resource_manager == WalResourceManager::Transaction &&
               record.record_type == WalRecordType::Abort;
    });
    EXPECT_TRUE(has_abort) << "expected a TX_ABORT WAL record after a failed statement";
}

TEST_F(QueryEngineWalTest, InsertEmitsHeapInsertBeforeCommit) {
    QueryEngine engine{catalog, registry, wal};

    ASSERT_TRUE(engine.execute("CREATE TABLE items (x INTEGER)").has_value())
        << engine.error_message();
    ASSERT_TRUE(engine.execute("INSERT INTO items VALUES (7)").has_value())
        << engine.error_message();

    auto records = wal.read_all();
    ASSERT_TRUE(records.has_value());

    std::size_t heap_insert_pos = records->size();
    std::size_t commit_pos = records->size();
    for (std::size_t index = 0; index < records->size(); ++index) {
        const auto& record = (*records)[index];
        if (record.resource_manager == WalResourceManager::Heap &&
            record.record_type == WalRecordType::HeapInsert) {
            heap_insert_pos = index;
        }
        if (record.resource_manager == WalResourceManager::Transaction &&
            record.record_type == WalRecordType::Commit && index > heap_insert_pos) {
            commit_pos = index;
        }
    }

    EXPECT_LT(heap_insert_pos, records->size()) << "expected a HEAP_INSERT WAL record";
    EXPECT_LT(commit_pos, records->size()) << "expected a TX_COMMIT WAL record";
    EXPECT_LT(heap_insert_pos, commit_pos) << "HEAP_INSERT must appear before TX_COMMIT in the WAL";
}

TEST_F(QueryEngineWalTest, CommittedInsertSurvivesRecovery) {
    // The users table is the first user relation opened; its OID is 1.
    // (OIDs assigned sequentially starting at 1 by the bootstrap catalog.)
    constexpr auto page_size = usize{512};
    constexpr auto users_oid = u32{1};

    {
        QueryEngine engine{catalog, registry, wal};
        ASSERT_TRUE(engine.execute("CREATE TABLE users (id INTEGER, name TEXT)").has_value())
            << engine.error_message();
        ASSERT_TRUE(engine.execute("INSERT INTO users VALUES (1, 'alice')").has_value())
            << engine.error_message();
        // Flush WAL so recovery can see all records.
        ASSERT_TRUE(wal.flush(wal.appended_lsn()).has_value());
    }

    // Grab the raw page bytes for the users relation so we can run recovery.
    auto table_bytes = factory.bytes_for(users_oid);
    ASSERT_NE(table_bytes, nullptr) << "users relation was never opened";

    // Simulate a crash+recovery: open a fresh PageStore on the same bytes, then
    // replay the WAL into an empty store.
    auto recovered_bytes = std::make_shared<std::vector<std::byte>>();
    SharedMemoryIO recovered_io{recovered_bytes};
    PageStore recovered_store;
    ASSERT_TRUE(
        recovered_store.open(recovered_io, PageStoreConfig{.page_size = page_size}).has_value());

    auto recovered = recover_heap(recovered_store, page_size, wal);
    ASSERT_TRUE(recovered.has_value()) << "recover_heap failed";

    EdbHeapEngine recovered_engine;
    ASSERT_TRUE(recovered_engine
                    .open(recovered_store,
                          EngineConfig{.page_size = page_size, .buffer_pool_pages = usize{4}})
                    .has_value());

    // Use a high observer tx_id so any committed tuple is visible.
    auto tuples = collect_visible(recovered_engine, *recovered->statuses, TxId{u64{100}});
    ASSERT_TRUE(tuples.has_value()) << "scan after recovery failed";
    EXPECT_EQ(tuples->size(), std::size_t{1}) << "expected exactly one tuple to survive recovery";
}
