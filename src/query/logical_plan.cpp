// src/query/logical_plan.cpp

#include "query/logical_plan.hpp"

#include <memory>
#include <utility>

namespace edb {

auto LogicalPlanner::build(BoundStmt stmt) -> Result<LogicalPlan> {
    if (auto* create_stmt = std::get_if<BoundCreateTableStmt>(&stmt); create_stmt != nullptr) {
        return LogicalPlan{.node = LogicalCreateTable{.stmt = std::move(*create_stmt)}};
    }
    if (auto* insert_stmt = std::get_if<BoundInsertStmt>(&stmt); insert_stmt != nullptr) {
        return LogicalPlan{.node = LogicalInsert{.stmt = std::move(*insert_stmt)}};
    }
    if (auto* select_stmt = std::get_if<BoundSelectStmt>(&stmt); select_stmt != nullptr) {
        return build_select(std::move(*select_stmt));
    }
    return plan_err("unsupported bound statement");
}

auto LogicalPlanner::error_message() const noexcept -> std::string_view {
    return last_error;
}

auto LogicalPlanner::build_select(BoundSelectStmt stmt) -> Result<LogicalPlan> {
    auto plan = std::make_unique<LogicalPlan>(
        LogicalPlan{.node = LogicalScan{.table = std::move(stmt.table)}});

    if (stmt.where.has_value()) {
        plan = std::make_unique<LogicalPlan>(LogicalPlan{
            .node = LogicalFilter{.input = std::move(plan), .predicate = std::move(*stmt.where)},
        });
    }

    return LogicalPlan{
        .node = LogicalProject{.input = std::move(plan), .items = std::move(stmt.items)},
    };
}

auto LogicalPlanner::plan_err(std::string_view msg) -> Result<LogicalPlan> {
    last_error = std::string{msg};
    return std::unexpected(Error::ExecutorError);
}

}  // namespace edb