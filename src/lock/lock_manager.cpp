// src/lock/lock_manager.cpp

#include "lock/lock_manager.hpp"

#include <algorithm>
#include <expected>
#include <tuple>

namespace edb {

auto LockTag::operator<=>(const LockTag& other) const noexcept {
    return std::tie(kind, relation_oid.value, tuple_id.page_id.value, tuple_id.slot_idx.value) <=>
           std::tie(other.kind, other.relation_oid.value, other.tuple_id.page_id.value,
                    other.tuple_id.slot_idx.value);
}

auto LockTag::operator==(const LockTag& other) const noexcept -> bool {
    return kind == other.kind && relation_oid == other.relation_oid &&
           tuple_id.page_id == other.tuple_id.page_id && tuple_id.slot_idx == other.tuple_id.slot_idx;
}

auto LockManager::acquire(TxId tx_id, LockTag tag, LockMode mode) -> VoidResult {
    if (tx_id.value == u64{0}) {
        return std::unexpected(Error::InvalidArgument);
    }

    std::unique_lock lock{latch};
    while (true) {
        auto holders = conflicting_holders(tx_id, tag, mode);
        if (holders.empty()) {
            remove_wait_edges_for(tx_id);
            grant_or_upgrade(tx_id, tag, mode);
            return {};
        }

        waits_for[tx_id] = std::move(holders);
        if (has_cycle_from(tx_id).value) {
            remove_wait_edges_for(tx_id);
            return std::unexpected(Error::DeadlockDetected);
        }
        cv.wait(lock);
    }
}

auto LockManager::release_all(TxId tx_id) -> void {
    std::scoped_lock lock{latch};
    for (auto lock_iter = locks.begin(); lock_iter != locks.end();) {
        auto& granted = lock_iter->second.granted;
        std::erase_if(granted, [tx_id](const GrantedLock& lock) { return lock.tx_id == tx_id; });
        if (granted.empty()) {
            lock_iter = locks.erase(lock_iter);
        } else {
            ++lock_iter;
        }
    }
    remove_wait_edges_for(tx_id);
    for (auto& [waiting_tx, blockers] : waits_for) {
        blockers.erase(tx_id);
    }
    cv.notify_all();
}

auto LockManager::waiting_count() const -> usize {
    std::scoped_lock lock{latch};
    return usize{waits_for.size()};
}

auto LockManager::compatible(LockMode existing, LockMode requested) -> b8 {
    return b8{existing == LockMode::Shared && requested == LockMode::Shared};
}

auto LockManager::covers(LockMode existing, LockMode requested) -> b8 {
    return b8{existing == requested || existing == LockMode::Exclusive};
}

auto LockManager::conflicting_holders(TxId tx_id, const LockTag& tag, LockMode mode) const
    -> std::set<TxId> {
    std::set<TxId> holders;
    const auto lock_iter = locks.find(tag);
    if (lock_iter == locks.end()) {
        return holders;
    }
    for (const auto& granted : lock_iter->second.granted) {
        if (granted.tx_id == tx_id) {
            continue;
        }
        if (!compatible(granted.mode, mode).value) {
            holders.insert(granted.tx_id);
        }
    }
    return holders;
}

auto LockManager::grant_or_upgrade(TxId tx_id, const LockTag& tag, LockMode mode) -> void {
    auto& granted = locks[tag].granted;
    auto existing = std::ranges::find_if(
        granted, [tx_id](const GrantedLock& lock) { return lock.tx_id == tx_id; });
    if (existing == granted.end()) {
        granted.push_back(GrantedLock{.tx_id = tx_id, .mode = mode});
        return;
    }
    if (!covers(existing->mode, mode).value) {
        existing->mode = mode;
    }
}

auto LockManager::has_cycle_from(TxId start) const -> b8 {
    std::set<TxId> seen;
    return has_cycle_dfs(CycleSearch{.start = start}, start, seen);
}

auto LockManager::has_cycle_dfs(CycleSearch search, TxId current, std::set<TxId>& seen) const
    -> b8 {
    const auto edge_iter = waits_for.find(current);
    if (edge_iter == waits_for.end()) {
        return b8{false};
    }
    for (const auto blocker : edge_iter->second) {
        if (blocker == search.start) {
            return b8{true};
        }
        if (seen.insert(blocker).second && has_cycle_dfs(search, blocker, seen).value) {
            return b8{true};
        }
    }
    return b8{false};
}

auto LockManager::remove_wait_edges_for(TxId tx_id) -> void {
    waits_for.erase(tx_id);
}

}  // namespace edb