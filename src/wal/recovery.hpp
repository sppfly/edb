#pragma once

// src/wal/recovery.hpp

#include <map>
#include <memory>
#include <span>
#include <vector>

#include "storage/engine/engine_ops.hpp"
#include "storage/page/page_store.hpp"
#include "transaction/visibility.hpp"
#include "utils/error.hpp"
#include "utils/primitives.hpp"
#include "wal/wal_manager.hpp"

namespace edb {

class RecoveredTransactionStatus final : public TransactionStatusReader {
   public:
    [[nodiscard]] auto status(TxId id) const -> Result<TxStatus> override;
    auto set_status(TxId id, TxStatus status) -> void;

   private:
    std::map<TxId, TxStatus> statuses;
};

struct HeapRecoveryResult {
    std::unique_ptr<RecoveredTransactionStatus> statuses;
};

[[nodiscard]] auto make_heap_insert_payload(TupleId id, std::span<const std::byte> tuple)
    -> Result<std::vector<std::byte>>;
[[nodiscard]] auto recover_heap(PageStore& store, usize page_size, const WalManager& wal)
    -> Result<HeapRecoveryResult>;

}  // namespace edb