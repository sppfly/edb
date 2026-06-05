#pragma once

// src/query/query_engine.hpp
//
// Thin SQL execution facade for the Phase 5 reference path.

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "catalog/catalog.hpp"
#include "lock/lock_manager.hpp"
#include "query/exec.hpp"
#include "transaction/transaction_manager.hpp"
#include "utils/error.hpp"
#include "wal/wal_manager.hpp"

namespace edb {

struct QueryResult {
    std::vector<ExecRow> rows;
};

class QueryEngine {
   public:
    // No-WAL constructor: autocommit transactions but no durability.
    QueryEngine(Catalog& catalog, const TypeRegistry& types) noexcept;

    // WAL constructor: heap inserts and commits are written to `wal` and
    // flushed before committed status is exposed.
    // pre: wal.open() has already been called
    QueryEngine(Catalog& catalog, const TypeRegistry& types, WalManager& wal) noexcept;

    [[nodiscard]] auto execute(std::string_view sql) -> Result<QueryResult>;
    [[nodiscard]] auto error_message() const noexcept -> std::string_view;

   private:
    auto query_err(std::string_view msg, Error error) -> Result<QueryResult>;
    auto abort_query(TxId tx_id, std::string_view msg, Error error) -> Result<QueryResult>;

    Catalog* catalog{nullptr};
    const TypeRegistry* types{nullptr};
    TransactionManager transactions;
    LockManager locks;
    WalManager* wal_manager{nullptr};
    std::unique_ptr<WalEmitter> wal_emitter_impl;  // owns the concrete WalEmitter
    std::string last_error;
};

}  // namespace edb