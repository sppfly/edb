// tests/unit/lock/lock_manager_test.cpp

#include "lock/lock_manager.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <future>
#include <thread>

using namespace edb;

namespace {

[[nodiscard]] auto relation_tag(u32 relation_oid) -> LockTag {
    return LockTag{.kind = LockTagKind::Relation,
                   .relation_oid = relation_oid,
                   .tuple_id = TupleId{.page_id = u64{0}, .slot_idx = u16{0}}};
}

auto wait_until_waiting(const LockManager& locks, usize expected) -> void {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{1};
    while (std::chrono::steady_clock::now() < deadline) {
        if (locks.waiting_count() == expected) {
            return;
        }
        std::this_thread::yield();
    }
    FAIL() << "timed out waiting for lock waiter count";
}

}  // namespace

TEST(LockManager, SharedLocksAreCompatible) {
    LockManager locks;
    const auto tag = relation_tag(u32{10});

    EXPECT_TRUE(locks.acquire(TxId{u64{1}}, tag, LockMode::Shared).has_value());
    EXPECT_TRUE(locks.acquire(TxId{u64{2}}, tag, LockMode::Shared).has_value());
    EXPECT_EQ(locks.waiting_count(), usize{0});
}

TEST(LockManager, ExclusiveLockWaitsUntilHolderReleases) {
    LockManager locks;
    const auto tag = relation_tag(u32{11});
    ASSERT_TRUE(locks.acquire(TxId{u64{1}}, tag, LockMode::Shared).has_value());

    auto waiter = std::async(std::launch::async, [&locks, tag] {
        return locks.acquire(TxId{u64{2}}, tag, LockMode::Exclusive);
    });
    wait_until_waiting(locks, usize{1});
    EXPECT_EQ(waiter.wait_for(std::chrono::milliseconds{1}), std::future_status::timeout);

    locks.release_all(TxId{u64{1}});
    ASSERT_EQ(waiter.wait_for(std::chrono::seconds{1}), std::future_status::ready);
    EXPECT_TRUE(waiter.get().has_value());
}

TEST(LockManager, TwoTransactionDeadlockIsDetected) {
    LockManager locks;
    const auto first_tag = relation_tag(u32{20});
    const auto second_tag = relation_tag(u32{21});
    ASSERT_TRUE(locks.acquire(TxId{u64{1}}, first_tag, LockMode::Exclusive).has_value());
    ASSERT_TRUE(locks.acquire(TxId{u64{2}}, second_tag, LockMode::Exclusive).has_value());

    auto waiter = std::async(std::launch::async, [&locks, second_tag] {
        return locks.acquire(TxId{u64{1}}, second_tag, LockMode::Exclusive);
    });
    wait_until_waiting(locks, usize{1});

    auto deadlock = locks.acquire(TxId{u64{2}}, first_tag, LockMode::Exclusive);
    ASSERT_FALSE(deadlock.has_value());
    EXPECT_EQ(deadlock.error(), Error::DeadlockDetected);

    locks.release_all(TxId{u64{2}});
    ASSERT_EQ(waiter.wait_for(std::chrono::seconds{1}), std::future_status::ready);
    EXPECT_TRUE(waiter.get().has_value());
}

TEST(LockManager, ReleaseAllWakesCompatibleWaiters) {
    LockManager locks;
    const auto tag = relation_tag(u32{30});
    ASSERT_TRUE(locks.acquire(TxId{u64{1}}, tag, LockMode::Exclusive).has_value());

    auto first_waiter = std::async(std::launch::async, [&locks, tag] {
        return locks.acquire(TxId{u64{2}}, tag, LockMode::Shared);
    });
    auto second_waiter = std::async(std::launch::async, [&locks, tag] {
        return locks.acquire(TxId{u64{3}}, tag, LockMode::Shared);
    });
    wait_until_waiting(locks, usize{2});

    locks.release_all(TxId{u64{1}});
    ASSERT_EQ(first_waiter.wait_for(std::chrono::seconds{1}), std::future_status::ready);
    ASSERT_EQ(second_waiter.wait_for(std::chrono::seconds{1}), std::future_status::ready);
    EXPECT_TRUE(first_waiter.get().has_value());
    EXPECT_TRUE(second_waiter.get().has_value());
}