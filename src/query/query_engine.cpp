// src/query/query_engine.cpp

#include "query/query_engine.hpp"

#include <utility>

#include "query/binder.hpp"
#include "query/logical_plan.hpp"
#include "query/parser.hpp"
#include "query/physical_plan.hpp"

namespace edb {

namespace {

class ManagerStatusReader final : public TransactionStatusReader {
   public:
    explicit ManagerStatusReader(const TransactionManager& transactions) noexcept
        : transactions{&transactions} {}

    [[nodiscard]] auto status(TxId id) const -> Result<TxStatus> override {
        if (transactions == nullptr) {
            return std::unexpected(Error::InvalidArgument);
        }
        return transactions->status(id);
    }

   private:
    const TransactionManager* transactions{nullptr};
};

}  // namespace

QueryEngine::QueryEngine(Catalog& catalog, const TypeRegistry& types) noexcept
    : catalog{&catalog}, types{&types} {}

auto QueryEngine::execute(std::string_view sql) -> Result<QueryResult> {
    if (catalog == nullptr || types == nullptr) {
        return query_err("query engine is not initialized", Error::InvalidArgument);
    }

    Parser parser{sql};
    auto parsed = parser.parse();
    if (!parsed) {
        return query_err(parser.error_message(), parsed.error());
    }
    if (parsed->size() != std::size_t{1}) {
        return query_err("execute expects exactly one SQL statement", Error::InvalidArgument);
    }

    auto tx = transactions.begin();
    if (!tx) {
        return query_err("transaction begin failed", tx.error());
    }
    const auto tx_id = tx->id;

    Binder binder{*catalog, *types};
    auto bound = binder.bind(parsed->front());
    if (!bound) {
        return abort_query(tx_id, binder.error_message(), bound.error());
    }

    LogicalPlanner logical_planner;
    auto logical = logical_planner.build(std::move(*bound));
    if (!logical) {
        return abort_query(tx_id, logical_planner.error_message(), logical.error());
    }

    PhysicalPlanner physical_planner;
    auto physical = physical_planner.build(std::move(*logical));
    if (!physical) {
        return abort_query(tx_id, physical_planner.error_message(), physical.error());
    }

    ManagerStatusReader status_reader{transactions};
    ExecBuilder exec_builder{
        *catalog, *types,
        ExecTransactionContext{.tx = &*tx, .statuses = &status_reader, .locks = &locks}};
    auto exec = exec_builder.build(std::move(*physical));
    if (!exec) {
        return abort_query(tx_id, exec_builder.error_message(), exec.error());
    }

    auto opened = (*exec)->open();
    if (!opened) {
        return abort_query(tx_id, "executor open failed", opened.error());
    }

    QueryResult result;
    while (true) {
        ExecRow row;
        auto next = (*exec)->next(row);
        if (!next) {
            const auto error = next.error();
            auto closed_after_error = (*exec)->close();
            if (!closed_after_error) {
                return abort_query(tx_id, "executor close after next failure failed",
                                   closed_after_error.error());
            }
            return abort_query(tx_id, "executor next failed", error);
        }
        if (!static_cast<bool>(*next)) {
            break;
        }
        result.rows.push_back(std::move(row));
    }

    auto closed = (*exec)->close();
    if (!closed) {
        return abort_query(tx_id, "executor close failed", closed.error());
    }
    auto committed = transactions.commit(tx_id);
    if (!committed) {
        locks.release_all(tx_id);
        return query_err("transaction commit failed", committed.error());
    }
    locks.release_all(tx_id);
    return result;
}

auto QueryEngine::error_message() const noexcept -> std::string_view {
    return last_error;
}

auto QueryEngine::query_err(std::string_view msg, Error error) -> Result<QueryResult> {
    last_error = std::string{msg};
    return std::unexpected(error);
}

auto QueryEngine::abort_query(TxId tx_id, std::string_view msg, Error error)
    -> Result<QueryResult> {
    const auto aborted = transactions.abort(tx_id);
    locks.release_all(tx_id);
    if (!aborted) {
        return query_err("transaction abort failed", aborted.error());
    }
    return query_err(msg, error);
}

}  // namespace edb