// tests/unit/transaction/transaction_manager_test.cpp

#include "transaction/transaction_manager.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <mutex>
#include <thread>
#include <vector>

using namespace edb;

namespace {

auto contains_tx(const std::vector<TxId>& ids, TxId id) -> bool {
    return std::ranges::find(ids, id) != ids.end();
}

}  // namespace

TEST(TransactionManager, BeginAllocatesMonotonicIdsAndInProgressStatus) {
    TransactionManager manager;

    auto first = manager.begin();
    ASSERT_TRUE(first.has_value());
    auto second = manager.begin();
    ASSERT_TRUE(second.has_value());

    EXPECT_EQ(first->id.value.value, u64{1}.value);
    EXPECT_EQ(second->id.value.value, u64{2}.value);
    EXPECT_EQ(manager.active_count().value, usize{2}.value);

    auto first_status = manager.status(first->id);
    auto second_status = manager.status(second->id);
    ASSERT_TRUE(first_status.has_value());
    ASSERT_TRUE(second_status.has_value());
    EXPECT_EQ(*first_status, TxStatus::InProgress);
    EXPECT_EQ(*second_status, TxStatus::InProgress);
}

TEST(TransactionManager, SnapshotCapturesTransactionsActiveBeforeBegin) {
    TransactionManager manager;

    auto first = manager.begin();
    ASSERT_TRUE(first.has_value());
    auto second = manager.begin();
    ASSERT_TRUE(second.has_value());
    auto third = manager.begin();
    ASSERT_TRUE(third.has_value());

    EXPECT_TRUE(first->snapshot.active.empty());
    ASSERT_EQ(second->snapshot.active.size(), std::size_t{1});
    EXPECT_EQ(second->snapshot.active[0], first->id);
    ASSERT_EQ(third->snapshot.active.size(), std::size_t{2});
    EXPECT_EQ(third->snapshot.active[0], first->id);
    EXPECT_EQ(third->snapshot.active[1], second->id);
    EXPECT_EQ(third->snapshot.xmin, first->id);
    EXPECT_EQ(third->snapshot.xmax, third->id);
}

TEST(TransactionManager, CommitAndAbortFinalizeStatusAndRemoveActiveTransactions) {
    TransactionManager manager;

    auto committed = manager.begin();
    auto aborted = manager.begin();
    ASSERT_TRUE(committed.has_value());
    ASSERT_TRUE(aborted.has_value());

    ASSERT_TRUE(manager.commit(committed->id).has_value());
    ASSERT_TRUE(manager.abort(aborted->id).has_value());
    EXPECT_EQ(manager.active_count().value, usize{0}.value);

    auto committed_status = manager.status(committed->id);
    auto aborted_status = manager.status(aborted->id);
    ASSERT_TRUE(committed_status.has_value());
    ASSERT_TRUE(aborted_status.has_value());
    EXPECT_EQ(*committed_status, TxStatus::Committed);
    EXPECT_EQ(*aborted_status, TxStatus::Aborted);
}

TEST(TransactionManager, RejectsUnknownOrAlreadyFinalizedTransactions) {
    TransactionManager manager;

    EXPECT_EQ(manager.commit(TxId{u64{404}}).error(), Error::NotFound);

    auto tx = manager.begin();
    ASSERT_TRUE(tx.has_value());
    ASSERT_TRUE(manager.commit(tx->id).has_value());
    EXPECT_EQ(manager.commit(tx->id).error(), Error::InvalidArgument);
    EXPECT_EQ(manager.abort(tx->id).error(), Error::InvalidArgument);
}

TEST(TransactionManager, CurrentSnapshotReportsActiveTransactions) {
    TransactionManager manager;

    auto first = manager.begin();
    auto second = manager.begin();
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());

    auto snapshot = manager.snapshot();
    EXPECT_EQ(snapshot.xmin, first->id);
    EXPECT_EQ(snapshot.xmax.value.value, u64{3}.value);
    ASSERT_EQ(snapshot.active.size(), std::size_t{2});
    EXPECT_TRUE(contains_tx(snapshot.active, first->id));
    EXPECT_TRUE(contains_tx(snapshot.active, second->id));

    ASSERT_TRUE(manager.commit(first->id).has_value());
    snapshot = manager.snapshot();
    EXPECT_EQ(snapshot.xmin, second->id);
    ASSERT_EQ(snapshot.active.size(), std::size_t{1});
    EXPECT_EQ(snapshot.active[0], second->id);
}

TEST(TransactionManager, ConcurrentBeginAllocatesUniqueTransactionIds) {
    TransactionManager manager;
    constexpr std::size_t thread_count = 8;
    constexpr std::size_t tx_per_thread = 64;

    std::mutex ids_latch;
    std::vector<TxId> ids;
    ids.reserve(thread_count * tx_per_thread);

    std::vector<std::thread> threads;
    threads.reserve(thread_count);
    for (std::size_t thread_index = 0; thread_index < thread_count; ++thread_index) {
        threads.emplace_back([&manager, &ids, &ids_latch]() {
            std::vector<TxId> local_ids;
            local_ids.reserve(tx_per_thread);
            for (std::size_t tx_index = 0; tx_index < tx_per_thread; ++tx_index) {
                auto tx = manager.begin();
                ASSERT_TRUE(tx.has_value());
                local_ids.push_back(tx->id);
            }

            std::lock_guard guard{ids_latch};
            ids.insert(ids.end(), local_ids.begin(), local_ids.end());
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    ASSERT_EQ(ids.size(), thread_count * tx_per_thread);
    std::ranges::sort(ids, [](TxId lhs, TxId rhs) { return lhs.value < rhs.value; });
    for (std::size_t index = 0; index < ids.size(); ++index) {
        EXPECT_EQ(ids[index].value.value, static_cast<std::uint64_t>(index + 1U));
    }
    EXPECT_EQ(manager.active_count().value, usize{ids.size()}.value);
}