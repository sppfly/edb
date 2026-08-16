// src/query/exec.cpp

#include "query/exec.hpp"

#include <compare>
#include <memory>
#include <optional>
#include <utility>

namespace edb {

namespace {

[[nodiscard]] auto bound_expr_type(const BoundExpr& expr) -> const BoundTypeRef& {
    if (const auto* literal = std::get_if<BoundLiteral>(&expr); literal != nullptr) {
        return literal->type;
    }
    if (const auto* column = std::get_if<BoundColumnRef>(&expr); column != nullptr) {
        return column->type;
    }
    return std::get<std::unique_ptr<BoundBinaryExpr>>(expr)->type;
}

auto find_column_value(const ExecRow& row, const BoundColumnRef& ref) -> Result<Value> {
    for (std::size_t index = 0; index < row.columns.size(); ++index) {
        if (row.columns[index].attnum == ref.attnum && row.columns[index].name == ref.name) {
            return row.values[index];
        }
    }
    return std::unexpected(Error::AnalyzerError);
}

auto make_bool_value(const Type& bool_type, b8 value) -> Result<Value> {
    auto bytes = bool_type.from_text(static_cast<bool>(value) ? "true" : "false");
    if (!bytes) {
        return std::unexpected(bytes.error());
    }
    return Value{.type_oid = bool_type.oid, .bytes = std::move(*bytes), .is_null = b8{false}};
}

auto value_as_bool(const Value& value, const TypeRegistry& types) -> Result<b8> {
    auto type = types.lookup(value.type_oid);
    if (!type) {
        return std::unexpected(type.error());
    }
    return b8{(*type)->to_text(value.bytes) == std::string{"true"}};
}

auto compare_values(BinaryOp op, const Value& left, const Value& right, const TypeRegistry& types)
    -> Result<b8> {
    if (static_cast<bool>(left.is_null) || static_cast<bool>(right.is_null)) {
        return b8{false};
    }

    auto type = types.lookup(left.type_oid);
    if (!type) {
        return std::unexpected(type.error());
    }

    const auto ordering = (*type)->compare(left.bytes, right.bytes);
    switch (op) {
        case BinaryOp::Eq:
            return b8{ordering == std::strong_ordering::equal};
        case BinaryOp::Neq:
            return b8{ordering != std::strong_ordering::equal};
        case BinaryOp::Lt:
            return b8{ordering == std::strong_ordering::less};
        case BinaryOp::Le:
            return b8{ordering == std::strong_ordering::less ||
                      ordering == std::strong_ordering::equal};
        case BinaryOp::Gt:
            return b8{ordering == std::strong_ordering::greater};
        case BinaryOp::Ge:
            return b8{ordering == std::strong_ordering::greater ||
                      ordering == std::strong_ordering::equal};
        case BinaryOp::And:
        case BinaryOp::Or:
            return std::unexpected(Error::InvalidArgument);
    }
    return std::unexpected(Error::InvalidArgument);
}

auto eval_expr(const BoundExpr& expr, const ExecRow& row, const TypeRegistry& types)
    -> Result<Value> {
    if (const auto* literal = std::get_if<BoundLiteral>(&expr); literal != nullptr) {
        return literal->value;
    }
    if (const auto* column = std::get_if<BoundColumnRef>(&expr); column != nullptr) {
        return find_column_value(row, *column);
    }

    const auto& binary = *std::get<std::unique_ptr<BoundBinaryExpr>>(expr);
    auto left = eval_expr(binary.left, row, types);
    if (!left) {
        return std::unexpected(left.error());
    }
    auto right = eval_expr(binary.right, row, types);
    if (!right) {
        return std::unexpected(right.error());
    }

    const auto bool_type = types.lookup("bool");
    if (!bool_type) {
        return std::unexpected(bool_type.error());
    }

    if (binary.op == BinaryOp::And || binary.op == BinaryOp::Or) {
        auto left_bool = value_as_bool(*left, types);
        if (!left_bool) {
            return std::unexpected(left_bool.error());
        }
        auto right_bool = value_as_bool(*right, types);
        if (!right_bool) {
            return std::unexpected(right_bool.error());
        }

        const auto result =
            binary.op == BinaryOp::And
                ? b8{static_cast<bool>(*left_bool) && static_cast<bool>(*right_bool)}
                : b8{static_cast<bool>(*left_bool) || static_cast<bool>(*right_bool)};
        return make_bool_value(**bool_type, result);
    }

    auto comparison = compare_values(binary.op, *left, *right, types);
    if (!comparison) {
        return std::unexpected(comparison.error());
    }
    return make_bool_value(**bool_type, *comparison);
}

auto output_column(const BoundExpr& expr, const std::optional<std::string>& alias,
                   std::size_t index) -> BoundColumnRef {
    if (alias.has_value()) {
        return BoundColumnRef{
            .relation_oid = u32{0},
            .attnum = u32{static_cast<std::uint32_t>(
                index + 1U)},  // raw-primitive: projected column index becomes 1-based attnum
            .name = *alias,
            .type = bound_expr_type(expr),
            .nullable = b8{true},
        };
    }
    if (const auto* column = std::get_if<BoundColumnRef>(&expr); column != nullptr) {
        return BoundColumnRef{
            .relation_oid = u32{0},
            .attnum = u32{static_cast<std::uint32_t>(
                index + 1U)},  // raw-primitive: projected column index becomes 1-based attnum
            .name = column->name,
            .type = column->type,
            .nullable = column->nullable,
        };
    }
    return BoundColumnRef{
        .relation_oid = u32{0},
        .attnum = u32{static_cast<std::uint32_t>(
            index + 1U)},  // raw-primitive: projected column index becomes 1-based attnum
        .name = "?column?",
        .type = bound_expr_type(expr),
        .nullable = b8{true},
    };
}

class CreateTableExecNode final : public ExecNode {
   public:
    CreateTableExecNode(Catalog& catalog, BoundCreateTableStmt stmt)
        : catalog{&catalog}, stmt{std::move(stmt)} {}

    auto open() -> VoidResult override {
        if (catalog == nullptr) {
            return std::unexpected(Error::InvalidArgument);
        }

        CreateTableSpec spec{.name = stmt.table_name, .columns = {}};
        spec.columns.reserve(stmt.columns.size());
        for (const auto& column : stmt.columns) {
            spec.columns.push_back(ColumnSchema{
                .name = column.name,
                .type_oid = column.type.oid,
                .nullable = column.nullable,
            });
        }

        const auto created = catalog->create_table(spec);
        if (!created) {
            if (stmt.if_not_exists && created.error() == Error::AlreadyExists) {
                opened = b8{true};
                done = b8{false};
                return {};
            }
            return std::unexpected(created.error());
        }
        opened = b8{true};
        done = b8{false};
        return {};
    }

    auto next(ExecRow& /*out*/) -> Result<b8> override {
        if (!opened) {
            return std::unexpected(Error::InvalidArgument);
        }
        if (done) {
            return b8{false};
        }
        done = b8{true};
        return b8{false};
    }

    auto close() -> VoidResult override {
        opened = b8{false};
        done = b8{false};
        return {};
    }

   private:
    Catalog* catalog{nullptr};
    BoundCreateTableStmt stmt;
    b8 opened{b8{false}};
    b8 done{b8{false}};
};

class InsertExecNode final : public ExecNode {
   public:
    InsertExecNode(Catalog& catalog, BoundInsertStmt stmt)
        : catalog{&catalog}, stmt{std::move(stmt)} {}

    auto open() -> VoidResult override {
        if (catalog == nullptr) {
            return std::unexpected(Error::InvalidArgument);
        }
        auto table = catalog->open_table(stmt.table.name);
        if (!table) {
            return std::unexpected(table.error());
        }

        for (const auto& row : stmt.rows) {
            std::vector<Value> values;
            values.reserve(row.size());
            for (const auto& literal : row) {
                values.push_back(literal.value);
            }
            auto inserted = (*table)->insert(values);
            if (!inserted) {
                return std::unexpected(inserted.error());
            }
        }
        opened = b8{true};
        done = b8{false};
        return {};
    }

    auto next(ExecRow& /*out*/) -> Result<b8> override {
        if (!opened) {
            return std::unexpected(Error::InvalidArgument);
        }
        if (done) {
            return b8{false};
        }
        done = b8{true};
        return b8{false};
    }

    auto close() -> VoidResult override {
        opened = b8{false};
        done = b8{false};
        return {};
    }

   private:
    Catalog* catalog{nullptr};
    BoundInsertStmt stmt;
    b8 opened{b8{false}};
    b8 done{b8{false}};
};

class SeqScanExecNode final : public ExecNode {
   public:
    SeqScanExecNode(Catalog& catalog, BoundTableRef table)
        : catalog{&catalog}, table{std::move(table)} {}

    auto open() -> VoidResult override {
        if (catalog == nullptr) {
            return std::unexpected(Error::InvalidArgument);
        }
        auto opened_table = catalog->open_table(table.name);
        if (!opened_table) {
            return std::unexpected(opened_table.error());
        }
        auto scanned = (*opened_table)->scan();
        if (!scanned) {
            return std::unexpected(scanned.error());
        }
        rows = std::move(*scanned);
        index = 0;
        opened = b8{true};
        return {};
    }

    auto next(ExecRow& out) -> Result<b8> override {
        if (!opened) {
            return std::unexpected(Error::InvalidArgument);
        }
        if (index >= rows.size()) {
            return b8{false};
        }
        out = ExecRow{.columns = table.columns, .values = rows[index]};
        ++index;
        return b8{true};
    }

    auto close() -> VoidResult override {
        rows.clear();
        index = 0;
        opened = b8{false};
        return {};
    }

   private:
    Catalog* catalog{nullptr};
    BoundTableRef table;
    std::vector<std::vector<Value>> rows;
    std::size_t index{0};
    b8 opened{b8{false}};
};

class FilterExecNode final : public ExecNode {
   public:
    FilterExecNode(std::unique_ptr<ExecNode> input, const TypeRegistry& types, BoundExpr predicate)
        : input{std::move(input)}, types{&types}, predicate{std::move(predicate)} {}

    auto open() -> VoidResult override {
        if (input == nullptr) {
            return std::unexpected(Error::InvalidArgument);
        }
        return input->open();
    }

    auto next(ExecRow& out) -> Result<b8> override {
        if (input == nullptr || types == nullptr) {
            return std::unexpected(Error::InvalidArgument);
        }

        ExecRow candidate;
        while (true) {
            auto has_row = input->next(candidate);
            if (!has_row) {
                return std::unexpected(has_row.error());
            }
            if (!static_cast<bool>(*has_row)) {
                return b8{false};
            }

            auto predicate_value = eval_expr(predicate, candidate, *types);
            if (!predicate_value) {
                return std::unexpected(predicate_value.error());
            }
            auto keep = value_as_bool(*predicate_value, *types);
            if (!keep) {
                return std::unexpected(keep.error());
            }
            if (static_cast<bool>(*keep)) {
                out = std::move(candidate);
                return b8{true};
            }
        }
    }

    auto close() -> VoidResult override {
        if (input == nullptr) {
            return std::unexpected(Error::InvalidArgument);
        }
        return input->close();
    }

   private:
    std::unique_ptr<ExecNode> input;
    const TypeRegistry* types{nullptr};
    BoundExpr predicate;
};

class ProjectExecNode final : public ExecNode {
   public:
    ProjectExecNode(std::unique_ptr<ExecNode> input, const TypeRegistry& types,
                    std::vector<BoundSelectItem> items)
        : input{std::move(input)}, types{&types}, items{std::move(items)} {}

    auto open() -> VoidResult override {
        if (input == nullptr) {
            return std::unexpected(Error::InvalidArgument);
        }
        return input->open();
    }

    auto next(ExecRow& out) -> Result<b8> override {
        if (input == nullptr || types == nullptr) {
            return std::unexpected(Error::InvalidArgument);
        }

        ExecRow input_row;
        auto has_row = input->next(input_row);
        if (!has_row) {
            return std::unexpected(has_row.error());
        }
        if (!static_cast<bool>(*has_row)) {
            return b8{false};
        }

        ExecRow projected;
        for (const auto& item : items) {
            if (std::holds_alternative<BoundStarItem>(item)) {
                for (std::size_t index = 0; index < input_row.values.size(); ++index) {
                    projected.columns.push_back(input_row.columns[index]);
                    projected.values.push_back(input_row.values[index]);
                }
                continue;
            }

            const auto& expr_item = std::get<BoundExprItem>(item);
            auto value = eval_expr(expr_item.expr, input_row, *types);
            if (!value) {
                return std::unexpected(value.error());
            }
            projected.columns.push_back(
                output_column(expr_item.expr, expr_item.alias, projected.columns.size()));
            projected.values.push_back(std::move(*value));
        }

        out = std::move(projected);
        return b8{true};
    }

    auto close() -> VoidResult override {
        if (input == nullptr) {
            return std::unexpected(Error::InvalidArgument);
        }
        return input->close();
    }

   private:
    std::unique_ptr<ExecNode> input;
    const TypeRegistry* types{nullptr};
    std::vector<BoundSelectItem> items;
};

}  // namespace

ExecBuilder::ExecBuilder(Catalog& catalog, const TypeRegistry& types) noexcept
    : catalog{&catalog}, types{&types} {}

auto ExecBuilder::build(PhysicalPlan plan) -> Result<std::unique_ptr<ExecNode>> {
    return build_node(std::move(plan.node));
}

auto ExecBuilder::error_message() const noexcept -> std::string_view {
    return last_error;
}

auto ExecBuilder::build_node(PhysicalPlan::Node node) -> Result<std::unique_ptr<ExecNode>> {
    if (catalog == nullptr || types == nullptr) {
        return std::unexpected(Error::InvalidArgument);
    }

    if (auto* create_stmt = std::get_if<PhysicalCreateTable>(&node); create_stmt != nullptr) {
        return std::make_unique<CreateTableExecNode>(*catalog, std::move(create_stmt->stmt));
    }
    if (auto* insert_stmt = std::get_if<PhysicalInsert>(&node); insert_stmt != nullptr) {
        return std::make_unique<InsertExecNode>(*catalog, std::move(insert_stmt->stmt));
    }
    if (auto* scan = std::get_if<PhysicalSeqScan>(&node); scan != nullptr) {
        return std::make_unique<SeqScanExecNode>(*catalog, std::move(scan->table));
    }
    if (auto* filter = std::get_if<PhysicalFilter>(&node); filter != nullptr) {
        if (filter->input == nullptr) {
            return exec_err("physical filter requires an input plan");
        }
        auto input = build(std::move(*filter->input));
        if (!input) {
            return std::unexpected(input.error());
        }
        return std::make_unique<FilterExecNode>(std::move(*input), *types,
                                                std::move(filter->predicate));
    }
    if (auto* project = std::get_if<PhysicalProject>(&node); project != nullptr) {
        if (project->input == nullptr) {
            return exec_err("physical project requires an input plan");
        }
        auto input = build(std::move(*project->input));
        if (!input) {
            return std::unexpected(input.error());
        }
        return std::make_unique<ProjectExecNode>(std::move(*input), *types,
                                                 std::move(project->items));
    }

    return exec_err("unsupported physical node");
}

auto ExecBuilder::exec_err(std::string_view msg) -> Result<std::unique_ptr<ExecNode>> {
    last_error = std::string{msg};
    return std::unexpected(Error::ExecutorError);
}

}  // namespace edb