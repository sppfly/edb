#pragma once

// src/query/logical_plan.hpp
//
// Backend-neutral logical plan nodes for the Phase 5 SQL subset.

#include <memory>
#include <string_view>
#include <variant>
#include <vector>

#include "query/binder.hpp"
#include "utils/error.hpp"

namespace edb {

struct LogicalPlan;

struct LogicalCreateTable {
    BoundCreateTableStmt stmt;
};

struct LogicalInsert {
    BoundInsertStmt stmt;
};

struct LogicalScan {
    BoundTableRef table;
};

struct LogicalFilter {
    std::unique_ptr<LogicalPlan> input;
    BoundExpr predicate;
};

struct LogicalProject {
    std::unique_ptr<LogicalPlan> input;
    std::vector<BoundSelectItem> items;
};

struct LogicalPlan {
    using Node =
        std::variant<LogicalCreateTable, LogicalInsert, LogicalScan, LogicalFilter, LogicalProject>;

    Node node;
};

class LogicalPlanner {
   public:
    LogicalPlanner() = default;

    [[nodiscard]] auto build(BoundStmt stmt) -> Result<LogicalPlan>;
    [[nodiscard]] auto error_message() const noexcept -> std::string_view;

   private:
    [[nodiscard]] static auto build_select(BoundSelectStmt stmt) -> Result<LogicalPlan>;
    auto plan_err(std::string_view msg) -> Result<LogicalPlan>;

    std::string last_error;
};

}  // namespace edb