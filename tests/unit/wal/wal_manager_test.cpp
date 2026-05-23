// tests/unit/wal/wal_manager_test.cpp

#include "wal/wal_manager.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

using namespace edb;

namespace {

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
        auto src = std::span<const std::byte>{*storage}.subspan(off, count);
        std::ranges::copy(src, buf.begin());
        return usize{count};
    }

    auto write_impl(u64 offset, std::span<const std::byte> buf) -> Result<usize> override {
        const auto off = offset.value;
        if ((off + buf.size()) > storage->size()) {
            storage->resize(off + buf.size());
        }
        auto dst = std::span<std::byte>{*storage}.subspan(off, buf.size());
        std::ranges::copy(buf, dst.begin());
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

auto payload(std::string_view text) -> std::vector<std::byte> {
    std::vector<std::byte> bytes;
    bytes.reserve(text.size());
    for (const auto ch : text) {
        bytes.push_back(std::byte{static_cast<unsigned char>(ch)});
    }
    return bytes;
}

}  // namespace

TEST(WalManager, RecordsRoundTripThroughWalFile) {
    auto bytes = std::make_shared<std::vector<std::byte>>();
    SharedMemoryIO io{bytes};
    WalManager wal{io};
    ASSERT_TRUE(wal.open().has_value());

    auto first = wal.append(WalAppendRecord{.prev_lsn = u64{0},
                                            .tx_id = TxId{u64{7}},
                                            .resource_manager = WalResourceManager::Heap,
                                            .record_type = WalRecordType::HeapInsert,
                                            .payload = payload("insert")});
    ASSERT_TRUE(first.has_value());
    auto second = wal.append(WalAppendRecord{.prev_lsn = *first,
                                             .tx_id = TxId{u64{7}},
                                             .resource_manager = WalResourceManager::Transaction,
                                             .record_type = WalRecordType::Commit,
                                             .payload = payload("commit")});
    ASSERT_TRUE(second.has_value());

    auto records = wal.read_all();
    ASSERT_TRUE(records.has_value());
    ASSERT_EQ(records->size(), std::size_t{2});
    EXPECT_EQ((*records)[0].lsn, *first);
    EXPECT_EQ((*records)[1].prev_lsn, *first);
    EXPECT_EQ((*records)[0].tx_id, TxId{u64{7}});
    EXPECT_EQ((*records)[0].resource_manager, WalResourceManager::Heap);
    EXPECT_EQ((*records)[0].record_type, WalRecordType::HeapInsert);
    EXPECT_EQ((*records)[0].payload, payload("insert"));
    EXPECT_LT(*first, *second);
}

TEST(WalManager, FlushPersistsRecordsThroughReopen) {
    auto bytes = std::make_shared<std::vector<std::byte>>();
    SharedMemoryIO io{bytes};
    WalManager wal{io};
    ASSERT_TRUE(wal.open().has_value());

    auto lsn = wal.append(WalAppendRecord{.tx_id = TxId{u64{3}},
                                          .record_type = WalRecordType::Commit,
                                          .payload = payload("done")});
    ASSERT_TRUE(lsn.has_value());
    ASSERT_TRUE(wal.flush(*lsn).has_value());
    EXPECT_EQ(wal.flushed_lsn(), *lsn);

    SharedMemoryIO reopened_io{bytes};
    WalManager reopened{reopened_io};
    ASSERT_TRUE(reopened.open().has_value());
    auto records = reopened.read_all();
    ASSERT_TRUE(records.has_value());
    ASSERT_EQ(records->size(), std::size_t{1});
    EXPECT_EQ((*records)[0].payload, payload("done"));
}

TEST(WalManager, ConcurrentAppendAllocatesIncreasingUniqueLsns) {
    auto bytes = std::make_shared<std::vector<std::byte>>();
    SharedMemoryIO io{bytes};
    WalManager wal{io};
    ASSERT_TRUE(wal.open().has_value());

    constexpr auto thread_count = std::size_t{4};
    constexpr auto records_per_thread = std::size_t{25};
    std::vector<std::thread> threads;
    threads.reserve(thread_count);
    for (std::size_t thread_index = 0; thread_index < thread_count; ++thread_index) {
        threads.emplace_back([&wal, thread_index] {
            for (std::size_t record_index = 0; record_index < records_per_thread; ++record_index) {
                auto lsn = wal.append(WalAppendRecord{
                    .tx_id = TxId{u64{static_cast<std::uint64_t>(thread_index + 1U)}},
                    .record_type = WalRecordType::Commit,
                    .payload = payload("x")});
                ASSERT_TRUE(lsn.has_value());
            }
        });
    }
    for (auto& thread : threads) {
        thread.join();
    }

    auto records = wal.read_all();
    ASSERT_TRUE(records.has_value());
    ASSERT_EQ(records->size(), thread_count * records_per_thread);
    for (std::size_t index = 1; index < records->size(); ++index) {
        EXPECT_LT((*records)[index - 1U].lsn, (*records)[index].lsn);
    }
}

TEST(WalManager, CorruptRecordCrcIsRejected) {
    auto bytes = std::make_shared<std::vector<std::byte>>();
    SharedMemoryIO io{bytes};
    WalManager wal{io};
    ASSERT_TRUE(wal.open().has_value());

    auto lsn = wal.append(WalAppendRecord{.tx_id = TxId{u64{9}},
                                          .record_type = WalRecordType::Commit,
                                          .payload = payload("stable")});
    ASSERT_TRUE(lsn.has_value());
    ASSERT_GT(bytes->size(), std::size_t{42});
    (*bytes)[42] ^= std::byte{0x01};

    auto records = wal.read_all();
    ASSERT_FALSE(records.has_value());
    EXPECT_EQ(records.error(), Error::Corruption);
}