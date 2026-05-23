// src/transaction/transaction_manager.cpp

#include "transaction/transaction_manager.hpp"

#include <expected>
#include <mutex>

namespace edb {

auto TransactionManager::begin() -> Result<Transaction> {
    std::lock_guard guard{latch};

    auto tx = Transaction{.id = next_id, .snapshot = snapshot_locked()};
    ++next_id.value;
    statuses.emplace(tx.id, TxStatus::InProgress);
    active.emplace(tx.id);
    return tx;
}

auto TransactionManager::commit(TxId id) -> VoidResult {
    std::lock_guard guard{latch};
    return finish_locked(id, TxStatus::Committed);
}

auto TransactionManager::abort(TxId id) -> VoidResult {
    std::lock_guard guard{latch};
    return finish_locked(id, TxStatus::Aborted);
}

auto TransactionManager::status(TxId id) const -> Result<TxStatus> {
    std::lock_guard guard{latch};

    const auto found = statuses.find(id);
    if (found == statuses.end()) {
        return std::unexpected(Error::NotFound);
    }
    return found->second;
}

auto TransactionManager::snapshot() const -> Snapshot {
    std::lock_guard guard{latch};
    return snapshot_locked();
}

auto TransactionManager::active_count() const -> usize {
    std::lock_guard guard{latch};
    return usize{active.size()};
}

auto TransactionManager::snapshot_locked() const -> Snapshot {
    auto snapshot = Snapshot{.xmin = next_id, .xmax = next_id, .active = {}};
    snapshot.active.reserve(active.size());

    for (const auto id : active) {
        snapshot.active.push_back(id);
        if (id.value < snapshot.xmin.value) {
            snapshot.xmin = id;
        }
    }
    return snapshot;
}

auto TransactionManager::finish_locked(TxId id, TxStatus final_status) -> VoidResult {
    const auto found = statuses.find(id);
    if (found == statuses.end()) {
        return std::unexpected(Error::NotFound);
    }
    if (found->second != TxStatus::InProgress) {
        return std::unexpected(Error::InvalidArgument);
    }

    found->second = final_status;
    active.erase(id);
    return {};
}

}  // namespace edb