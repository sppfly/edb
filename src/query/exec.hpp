#pragma once

// src/query/exec.hpp
//
// Reference row-oriented executor for the Phase 5 SQL subset.

#include <memory>
#include <string_view>
#include <vector>

#include "catalog/catalog.hpp"
#include "query/physical_plan.hpp"
#include "types/row_codec.hpp"
#include "utils/error.hpp"

namespace edb {

struct ExecRow {
    std::vector<BoundColumnRef> columns;
    std::vector<Value> values;
};

class ExecNode {
public:
    ExecNode() = default;
    ExecNode(const ExecNode&) = delete;
    auto operator=(const ExecNode&) -> ExecNode& = delete;
    ExecNode(ExecNode&&) = delete;
    auto operator=(ExecNode&&) -> ExecNode& = delete;
    virtual ~ExecNode() = default;

    virtual auto open() -> VoidResult = 0;
    virtual auto next(ExecRow& out) -> Result<b8> = 0;
    virtual auto close() -> VoidResult = 0;
};

class ExecBuilder {
public:
    ExecBuilder(Catalog& catalog, const TypeRegistry& types) noexcept;

    [[nodiscard]] auto build(PhysicalPlan plan) -> Result<std::unique_ptr<ExecNode>>;
    [[nodiscard]] auto error_message() const noexcept -> std::string_view;

private:
    [[nodiscard]] auto build_node(PhysicalPlan::Node node) -> Result<std::unique_ptr<ExecNode>>;
    auto exec_err(std::string_view msg) -> Result<std::unique_ptr<ExecNode>>;

    Catalog* catalog{nullptr};
    const TypeRegistry* types{nullptr};
    std::string last_error;
};

}  // namespace edb