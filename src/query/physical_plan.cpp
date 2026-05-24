// src/query/physical_plan.cpp

#include "query/physical_plan.hpp"

#include <memory>
#include <utility>

namespace edb {

auto PhysicalPlanner::build(LogicalPlan plan) -> Result<PhysicalPlan> {
    return build_node(std::move(plan.node));
}

auto PhysicalPlanner::error_message() const noexcept -> std::string_view {
    return last_error;
}

auto PhysicalPlanner::build_node(LogicalPlan::Node node) -> Result<PhysicalPlan> {
    if (auto* create_stmt = std::get_if<LogicalCreateTable>(&node); create_stmt != nullptr) {
        return PhysicalPlan{.node = PhysicalCreateTable{.stmt = std::move(create_stmt->stmt)}};
    }
    if (auto* insert_stmt = std::get_if<LogicalInsert>(&node); insert_stmt != nullptr) {
        return PhysicalPlan{.node = PhysicalInsert{.stmt = std::move(insert_stmt->stmt)}};
    }
    if (auto* scan = std::get_if<LogicalScan>(&node); scan != nullptr) {
        return PhysicalPlan{.node = PhysicalSeqScan{.table = std::move(scan->table)}};
    }
    if (auto* filter = std::get_if<LogicalFilter>(&node); filter != nullptr) {
        auto input = build_unary_input(std::move(filter->input));
        if (!input) {
            return std::unexpected(input.error());
        }
        return PhysicalPlan{
            .node = PhysicalFilter{.input = std::move(*input),
                                   .predicate = std::move(filter->predicate)},
        };
    }
    if (auto* project = std::get_if<LogicalProject>(&node); project != nullptr) {
        auto input = build_unary_input(std::move(project->input));
        if (!input) {
            return std::unexpected(input.error());
        }
        return PhysicalPlan{
            .node = PhysicalProject{.input = std::move(*input), .items = std::move(project->items)},
        };
    }
    return plan_err("unsupported logical node");
}

auto PhysicalPlanner::build_unary_input(std::unique_ptr<LogicalPlan> input)
    -> Result<std::unique_ptr<PhysicalPlan>> {
    if (input == nullptr) {
        return std::unexpected(Error::ExecutorError);
    }

    auto plan = build(std::move(*input));
    if (!plan) {
        return std::unexpected(plan.error());
    }
    return std::make_unique<PhysicalPlan>(std::move(*plan));
}

auto PhysicalPlanner::plan_err(std::string_view msg) -> Result<PhysicalPlan> {
    last_error = std::string{msg};
    return std::unexpected(Error::ExecutorError);
}

}  // namespace edb