#pragma once

// src/query/physical_plan.hpp
//
// Reference row-oriented physical plan nodes for the Phase 5 SQL subset.

#include <memory>
#include <string_view>
#include <variant>
#include <vector>

#include "query/logical_plan.hpp"
#include "utils/error.hpp"

namespace edb {

struct PhysicalPlan;

struct PhysicalCreateTable {
    BoundCreateTableStmt stmt;
};

struct PhysicalInsert {
    BoundInsertStmt stmt;
};

struct PhysicalSeqScan {
    BoundTableRef table;
};

struct PhysicalFilter {
    std::unique_ptr<PhysicalPlan> input;
    BoundExpr predicate;
};

struct PhysicalProject {
    std::unique_ptr<PhysicalPlan> input;
    std::vector<BoundSelectItem> items;
};

struct PhysicalPlan {
    using Node = std::variant<PhysicalCreateTable, PhysicalInsert, PhysicalSeqScan, PhysicalFilter,
                              PhysicalProject>;

    Node node;
};

class PhysicalPlanner {
public:
    PhysicalPlanner() = default;

    [[nodiscard]] auto build(LogicalPlan plan) -> Result<PhysicalPlan>;
    [[nodiscard]] auto error_message() const noexcept -> std::string_view;

private:
    [[nodiscard]] auto build_node(LogicalPlan::Node node) -> Result<PhysicalPlan>;
    [[nodiscard]] auto build_unary_input(std::unique_ptr<LogicalPlan> input)
        -> Result<std::unique_ptr<PhysicalPlan>>;
    auto plan_err(std::string_view msg) -> Result<PhysicalPlan>;

    std::string last_error;
};

}  // namespace edb