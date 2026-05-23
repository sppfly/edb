// src/transaction/visibility.cpp

#include "transaction/visibility.hpp"

#include <algorithm>
#include <expected>

namespace edb {

namespace {

[[nodiscard]] auto is_valid_tx(TxId id) -> bool { return id.value != u64{0}; }

[[nodiscard]] auto is_active_in_snapshot(const Snapshot& snapshot, TxId id) -> bool {
    return std::ranges::find(snapshot.active, id) != snapshot.active.end();
}

[[nodiscard]] auto committed_before_snapshot(TxId id, const Snapshot& snapshot) -> bool {
    return id.value < snapshot.xmax.value && !is_active_in_snapshot(snapshot, id);
}

}  // namespace

auto Visibility::is_visible(const TupleHeader& tuple, const VisibilityContext& context,
                            const TransactionStatusReader& statuses) -> Result<b8> {
    if (!is_valid_tx(tuple.xmin)) {
        return std::unexpected(Error::InvalidArgument);
    }

    if (tuple.xmin != context.current_tx) {
        auto xmin_status = statuses.status(tuple.xmin);
        if (!xmin_status) {
            return std::unexpected(xmin_status.error());
        }
        if (*xmin_status != TxStatus::Committed ||
            !committed_before_snapshot(tuple.xmin, context.snapshot)) {
            return b8{false};
        }
    }

    if (!is_valid_tx(tuple.xmax)) {
        return b8{true};
    }

    if (tuple.xmax == context.current_tx) {
        return b8{false};
    }

    auto xmax_status = statuses.status(tuple.xmax);
    if (!xmax_status) {
        return std::unexpected(xmax_status.error());
    }
    if (*xmax_status == TxStatus::Aborted || *xmax_status == TxStatus::InProgress) {
        return b8{true};
    }

    return b8{!committed_before_snapshot(tuple.xmax, context.snapshot)};
}

}  // namespace edb