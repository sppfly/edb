// tests/unit/transaction/visibility_test.cpp

#include "transaction/visibility.hpp"

#include <gtest/gtest.h>

#include <map>
#include <vector>

using namespace edb;

namespace {

class FakeStatusReader final : public TransactionStatusReader {
   public:
    auto set(TxId id, TxStatus tx_status) -> void { statuses[id] = tx_status; }

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

auto snapshot(TxId xmax, std::vector<TxId> active = {}) -> Snapshot {
    auto xmin = xmax;
    for (const auto id : active) {
        if (id.value < xmin.value) {
            xmin = id;
        }
    }
    return Snapshot{.xmin = xmin, .xmax = xmax, .active = std::move(active)};
}

auto visible(const TupleHeader& tuple, const VisibilityContext& context,
             const TransactionStatusReader& statuses) -> b8 {
    auto result = Visibility::is_visible(tuple, context, statuses);
    EXPECT_TRUE(result.has_value());
    return *result;
}

}  // namespace

TEST(Visibility, CommittedInsertBeforeSnapshotIsVisible) {
    FakeStatusReader statuses;
    statuses.set(TxId{u64{1}}, TxStatus::Committed);

    const auto tuple = TupleHeader{.xmin = TxId{u64{1}}, .xmax = TxId{u64{0}}, .flags = u16{0}};
    const auto context =
        VisibilityContext{.snapshot = snapshot(TxId{u64{3}}), .current_tx = TxId{u64{2}}};

    EXPECT_TRUE(static_cast<bool>(visible(tuple, context, statuses)));
}

TEST(Visibility, InProgressInsertByOtherTransactionIsInvisible) {
    FakeStatusReader statuses;
    statuses.set(TxId{u64{1}}, TxStatus::InProgress);

    const auto tuple = TupleHeader{.xmin = TxId{u64{1}}, .xmax = TxId{u64{0}}, .flags = u16{0}};
    const auto context = VisibilityContext{.snapshot = snapshot(TxId{u64{3}}, {TxId{u64{1}}}),
                                           .current_tx = TxId{u64{2}}};

    EXPECT_FALSE(static_cast<bool>(visible(tuple, context, statuses)));
}

TEST(Visibility, OwnInsertIsVisible) {
    FakeStatusReader statuses;

    const auto tuple = TupleHeader{.xmin = TxId{u64{2}}, .xmax = TxId{u64{0}}, .flags = u16{0}};
    const auto context =
        VisibilityContext{.snapshot = snapshot(TxId{u64{2}}), .current_tx = TxId{u64{2}}};

    EXPECT_TRUE(static_cast<bool>(visible(tuple, context, statuses)));
}

TEST(Visibility, AbortedInsertIsInvisible) {
    FakeStatusReader statuses;
    statuses.set(TxId{u64{1}}, TxStatus::Aborted);

    const auto tuple = TupleHeader{.xmin = TxId{u64{1}}, .xmax = TxId{u64{0}}, .flags = u16{0}};
    const auto context =
        VisibilityContext{.snapshot = snapshot(TxId{u64{3}}), .current_tx = TxId{u64{2}}};

    EXPECT_FALSE(static_cast<bool>(visible(tuple, context, statuses)));
}

TEST(Visibility, InsertCommittedAfterSnapshotIsInvisible) {
    FakeStatusReader statuses;
    statuses.set(TxId{u64{5}}, TxStatus::Committed);

    const auto tuple = TupleHeader{.xmin = TxId{u64{5}}, .xmax = TxId{u64{0}}, .flags = u16{0}};
    const auto context =
        VisibilityContext{.snapshot = snapshot(TxId{u64{4}}), .current_tx = TxId{u64{3}}};

    EXPECT_FALSE(static_cast<bool>(visible(tuple, context, statuses)));
}

TEST(Visibility, CommittedDeleteBeforeSnapshotHidesTuple) {
    FakeStatusReader statuses;
    statuses.set(TxId{u64{1}}, TxStatus::Committed);
    statuses.set(TxId{u64{2}}, TxStatus::Committed);

    const auto tuple = TupleHeader{.xmin = TxId{u64{1}}, .xmax = TxId{u64{2}}, .flags = u16{0}};
    const auto context =
        VisibilityContext{.snapshot = snapshot(TxId{u64{4}}), .current_tx = TxId{u64{3}}};

    EXPECT_FALSE(static_cast<bool>(visible(tuple, context, statuses)));
}

TEST(Visibility, InProgressDeleteByOtherTransactionKeepsTupleVisible) {
    FakeStatusReader statuses;
    statuses.set(TxId{u64{1}}, TxStatus::Committed);
    statuses.set(TxId{u64{3}}, TxStatus::InProgress);

    const auto tuple = TupleHeader{.xmin = TxId{u64{1}}, .xmax = TxId{u64{3}}, .flags = u16{0}};
    const auto context = VisibilityContext{.snapshot = snapshot(TxId{u64{4}}, {TxId{u64{3}}}),
                                           .current_tx = TxId{u64{2}}};

    EXPECT_TRUE(static_cast<bool>(visible(tuple, context, statuses)));
}

TEST(Visibility, AbortedDeleteKeepsTupleVisible) {
    FakeStatusReader statuses;
    statuses.set(TxId{u64{1}}, TxStatus::Committed);
    statuses.set(TxId{u64{2}}, TxStatus::Aborted);

    const auto tuple = TupleHeader{.xmin = TxId{u64{1}}, .xmax = TxId{u64{2}}, .flags = u16{0}};
    const auto context =
        VisibilityContext{.snapshot = snapshot(TxId{u64{4}}), .current_tx = TxId{u64{3}}};

    EXPECT_TRUE(static_cast<bool>(visible(tuple, context, statuses)));
}

TEST(Visibility, OwnDeleteHidesTupleFromSelf) {
    FakeStatusReader statuses;
    statuses.set(TxId{u64{1}}, TxStatus::Committed);

    const auto tuple = TupleHeader{.xmin = TxId{u64{1}}, .xmax = TxId{u64{2}}, .flags = u16{0}};
    const auto context =
        VisibilityContext{.snapshot = snapshot(TxId{u64{2}}), .current_tx = TxId{u64{2}}};

    EXPECT_FALSE(static_cast<bool>(visible(tuple, context, statuses)));
}

TEST(Visibility, MissingTransactionStatusPropagatesError) {
    FakeStatusReader statuses;

    const auto tuple = TupleHeader{.xmin = TxId{u64{1}}, .xmax = TxId{u64{0}}, .flags = u16{0}};
    const auto context =
        VisibilityContext{.snapshot = snapshot(TxId{u64{3}}), .current_tx = TxId{u64{2}}};

    auto result = Visibility::is_visible(tuple, context, statuses);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), Error::NotFound);
}

TEST(Visibility, InvalidInsertingTransactionIsRejected) {
    FakeStatusReader statuses;

    const auto tuple = TupleHeader{.xmin = TxId{u64{0}}, .xmax = TxId{u64{0}}, .flags = u16{0}};
    const auto context =
        VisibilityContext{.snapshot = snapshot(TxId{u64{3}}), .current_tx = TxId{u64{2}}};

    auto result = Visibility::is_visible(tuple, context, statuses);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), Error::InvalidArgument);
}