#pragma once

// src/transaction/visibility.hpp
//
// Pure MVCC tuple visibility rules for Phase 6b.

#include "transaction/transaction_manager.hpp"
#include "utils/error.hpp"
#include "utils/primitives.hpp"

namespace edb {

struct TupleHeader {
    TxId xmin{u64{0}};
    TxId xmax{u64{0}};
    u16  flags{0};
};

class TransactionStatusReader {
   public:
    TransactionStatusReader() = default;
    TransactionStatusReader(const TransactionStatusReader&) = delete;
    auto operator=(const TransactionStatusReader&) -> TransactionStatusReader& = delete;
    TransactionStatusReader(TransactionStatusReader&&) = delete;
    auto operator=(TransactionStatusReader&&) -> TransactionStatusReader& = delete;
    virtual ~TransactionStatusReader() = default;

    [[nodiscard]] virtual auto status(TxId id) const -> Result<TxStatus> = 0;
};

struct VisibilityContext {
    Snapshot snapshot;
    TxId     current_tx{u64{0}};
};

class Visibility {
   public:
    [[nodiscard]] static auto is_visible(const TupleHeader& tuple,
                                         const VisibilityContext& context,
                                         const TransactionStatusReader& statuses) -> Result<b8>;
};

}  // namespace edb