#pragma once

// src/lock/lock_manager.hpp

#include <condition_variable>
#include <map>
#include <mutex>
#include <set>
#include <vector>

#include "storage/engine/engine_ops.hpp"
#include "transaction/transaction_manager.hpp"
#include "utils/error.hpp"
#include "utils/primitives.hpp"

namespace edb {

enum class LockTagKind : std::uint8_t {
    Relation,
    Tuple,
};

enum class LockMode : std::uint8_t {
    Shared,
    Exclusive,
};

struct LockTag {
    LockTagKind kind{LockTagKind::Relation};
    u32 relation_oid{0};
    TupleId tuple_id{.page_id = u64{0}, .slot_idx = u16{0}};

    [[nodiscard]] auto operator<=>(const LockTag& other) const noexcept;
    [[nodiscard]] auto operator==(const LockTag& other) const noexcept -> bool;
};

class LockManager {
   public:
    [[nodiscard]] auto acquire(TxId tx_id, LockTag tag, LockMode mode) -> VoidResult;
    auto release_all(TxId tx_id) -> void;
    [[nodiscard]] auto waiting_count() const -> usize;

   private:
    struct GrantedLock {
        TxId tx_id{u64{0}};
        LockMode mode{LockMode::Shared};
    };

    struct LockState {
        std::vector<GrantedLock> granted;
    };

    struct CycleSearch {
        TxId start{u64{0}};
    };

    [[nodiscard]] static auto compatible(LockMode existing, LockMode requested) -> b8;
    [[nodiscard]] static auto covers(LockMode existing, LockMode requested) -> b8;
    [[nodiscard]] auto conflicting_holders(TxId tx_id, const LockTag& tag, LockMode mode) const
        -> std::set<TxId>;
    auto grant_or_upgrade(TxId tx_id, const LockTag& tag, LockMode mode) -> void;
    [[nodiscard]] auto has_cycle_from(TxId start) const -> b8;
    [[nodiscard]] auto has_cycle_dfs(CycleSearch search, TxId current, std::set<TxId>& seen) const
        -> b8;
    auto remove_wait_edges_for(TxId tx_id) -> void;

    mutable std::mutex latch;
    std::condition_variable cv;
    std::map<LockTag, LockState> locks;
    std::map<TxId, std::set<TxId>> waits_for;
};

}  // namespace edb