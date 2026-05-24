#pragma once

// src/transaction/transaction_manager.hpp
//
// Phase 6a transaction ID, status, and snapshot management.

#include <cstdint>
#include <map>
#include <mutex>
#include <set>
#include <vector>

#include "utils/error.hpp"
#include "utils/primitives.hpp"

namespace edb {

struct TxId {
    u64 value{0};

    constexpr auto operator<=>(const TxId&) const noexcept = default;
    constexpr auto operator==(const TxId&) const noexcept -> bool = default;
};

enum class TxStatus : std::uint8_t {  // raw-primitive: enum base type requires stdint typedef
    InProgress,
    Committed,
    Aborted,
};

struct Snapshot {
    TxId xmin{u64{0}};
    TxId xmax{u64{0}};
    std::vector<TxId> active;
};

struct Transaction {
    TxId id{u64{0}};
    Snapshot snapshot;
};

class TransactionManager {
   public:
    TransactionManager() = default;
    TransactionManager(const TransactionManager&) = delete;
    auto operator=(const TransactionManager&) -> TransactionManager& = delete;
    TransactionManager(TransactionManager&&) = delete;
    auto operator=(TransactionManager&&) -> TransactionManager& = delete;
    ~TransactionManager() = default;

    [[nodiscard]] auto begin() -> Result<Transaction>;
    [[nodiscard]] auto commit(TxId id) -> VoidResult;
    [[nodiscard]] auto abort(TxId id) -> VoidResult;
    [[nodiscard]] auto status(TxId id) const -> Result<TxStatus>;
    [[nodiscard]] auto snapshot() const -> Snapshot;
    [[nodiscard]] auto active_count() const -> usize;

   private:
    [[nodiscard]] auto snapshot_locked() const -> Snapshot;
    [[nodiscard]] auto finish_locked(TxId id, TxStatus final_status) -> VoidResult;

    mutable std::mutex latch;
    TxId next_id{u64{1}};
    std::map<TxId, TxStatus> statuses;
    std::set<TxId> active;
};

}  // namespace edb