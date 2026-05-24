// src/query/binder.cpp

#include "query/binder.hpp"

#include <algorithm>
#include <cstdint>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>

namespace edb {

namespace {

constexpr auto ascii_lower(char ch) noexcept -> char {
    return (ch >= 'A' && ch <= 'Z') ? static_cast<char>(ch + ('a' - 'A')) : ch;
}

auto lower_ascii(std::string_view text) -> std::string {
    std::string lowered{text};
    std::ranges::transform(lowered, lowered.begin(), [](char ch) { return ascii_lower(ch); });
    return lowered;
}

auto canonical_type_name(const TypeName& type_name) -> std::string {
    auto lowered = lower_ascii(type_name.name);
    if (lowered == "integer" || lowered == "int" || lowered == "int4") {
        return "int32";
    }
    if (lowered == "bigint" || lowered == "int8") {
        return "int64";
    }
    if (lowered == "double precision" || lowered == "double" || lowered == "float8") {
        return "float64";
    }
    if (lowered == "boolean") {
        return "bool";
    }
    if (lowered == "varchar" || lowered == "character varying") {
        return "text";
    }
    return lowered;
}

auto normalized_column_name(std::string_view name) -> std::string {
    return lower_ascii(name);
}

[[nodiscard]] auto bound_expr_type(const BoundExpr& expr) -> const BoundTypeRef& {
    if (const auto* literal = std::get_if<BoundLiteral>(&expr); literal != nullptr) {
        return literal->type;
    }
    if (const auto* column = std::get_if<BoundColumnRef>(&expr); column != nullptr) {
        return column->type;
    }
    return std::get<std::unique_ptr<BoundBinaryExpr>>(expr)->type;
}

auto is_bool_expr(const BoundExpr& expr, const BoundTypeRef& bool_type) -> bool {
    return bound_expr_type(expr).oid == bool_type.oid;
}

auto literal_text(const Literal& literal) -> std::optional<std::string> {
    return std::visit(
        [](const auto& value) -> std::optional<std::string> {
            using LiteralT = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<LiteralT, IntLiteral> ||
                          std::is_same_v<LiteralT, FloatLiteral>) {
                return std::to_string(
                    value.value.value);  // raw-primitive: std::to_string uses primitive scalars
            } else if constexpr (std::is_same_v<LiteralT, StrLiteral>) {
                return value.value;
            } else if constexpr (std::is_same_v<LiteralT, BoolLiteral>) {
                return static_cast<bool>(value.value) ? "true" : "false";
            } else {
                return std::nullopt;
            }
        },
        literal);
}

auto make_attnum(std::size_t index) -> u32 {
    return u32{static_cast<std::uint32_t>(
        index + 1U)};  // raw-primitive: schema index narrows to catalog attnum
}

}  // namespace

Binder::Binder(Catalog& catalog, const TypeRegistry& types) noexcept
    : catalog{&catalog}, types{&types} {}

auto Binder::bind(const Stmt& stmt) -> Result<BoundStmt> {
    if (const auto* create_stmt = std::get_if<CreateTableStmt>(&stmt); create_stmt != nullptr) {
        auto bound = bind_create_table(*create_stmt);
        if (!bound) {
            return std::unexpected(bound.error());
        }
        return BoundStmt{std::move(*bound)};
    }
    if (const auto* insert_stmt = std::get_if<InsertStmt>(&stmt); insert_stmt != nullptr) {
        auto bound = bind_insert(*insert_stmt);
        if (!bound) {
            return std::unexpected(bound.error());
        }
        return BoundStmt{std::move(*bound)};
    }
    if (const auto* select_stmt = std::get_if<SelectStmt>(&stmt); select_stmt != nullptr) {
        auto bound = bind_select(*select_stmt);
        if (!bound) {
            return std::unexpected(bound.error());
        }
        return BoundStmt{std::move(*bound)};
    }

    bind_err("binding for this statement kind is not implemented yet");
    return std::unexpected(Error::NotSupported);
}

auto Binder::error_message() const noexcept -> std::string_view {
    return last_error;
}

auto Binder::bind_create_table(const CreateTableStmt& stmt) -> Result<BoundCreateTableStmt> {
    if (catalog == nullptr) {
        bind_err("binder has no catalog");
        return std::unexpected(Error::InvalidArgument);
    }

    std::unordered_set<std::string> seen_columns;
    std::vector<BoundColumnDef> columns;
    columns.reserve(stmt.columns.size());

    for (const auto& column : stmt.columns) {
        const auto dedup_name = normalized_column_name(column.name);
        if (seen_columns.contains(dedup_name)) {
            bind_err(std::format("duplicate column '{}' in CREATE TABLE '{}'", column.name,
                                 stmt.table_name));
            return std::unexpected(Error::AnalyzerError);
        }
        seen_columns.emplace(dedup_name);

        const auto type_name = canonical_type_name(column.type);
        auto type = catalog->get_type(type_name);
        if (!type) {
            bind_err(
                std::format("unknown type '{}' for column '{}'", column.type.name, column.name));
            return std::unexpected(type.error() == Error::NotFound ? Error::TypeNotFound
                                                                   : type.error());
        }

        columns.emplace_back(BoundColumnDef{
            .name = column.name,
            .type = BoundTypeRef{.oid = type->oid, .name = type->name, .param = column.type.param},
            .nullable = b8{!static_cast<bool>(column.not_null)},
            .primary_key = column.primary_key,
            .unique_constraint = column.unique_constraint,
        });
    }

    return BoundCreateTableStmt{
        .table_name = stmt.table_name,
        .columns = std::move(columns),
        .if_not_exists = stmt.if_not_exists,
    };
}

auto Binder::bind_insert(const InsertStmt& stmt) -> Result<BoundInsertStmt> {
    auto table = bind_table(stmt.table_name);
    if (!table) {
        return std::unexpected(table.error());
    }

    std::vector<BoundColumnRef> target_columns;
    if (stmt.column_names.empty()) {
        target_columns = table->columns;
    } else {
        std::unordered_set<std::string> seen_columns;
        target_columns.reserve(stmt.column_names.size());
        for (const auto& column_name : stmt.column_names) {
            auto bound_column = bind_column_ref(ColumnRef{.name = column_name}, *table);
            if (!bound_column) {
                return std::unexpected(bound_column.error());
            }
            const auto normalized = normalized_column_name(bound_column->name);
            if (seen_columns.contains(normalized)) {
                bind_err(std::format("duplicate target column '{}' in INSERT into '{}'",
                                     bound_column->name, stmt.table_name));
                return std::unexpected(Error::AnalyzerError);
            }
            seen_columns.emplace(normalized);
            target_columns.push_back(std::move(*bound_column));
        }
    }

    std::vector<std::vector<BoundLiteral>> bound_rows;
    bound_rows.reserve(stmt.rows.size());
    for (const auto& row : stmt.rows) {
        if (row.size() != target_columns.size()) {
            bind_err(std::format("INSERT row has {} values but target column list has {} entries",
                                 row.size(), target_columns.size()));
            return std::unexpected(Error::AnalyzerError);
        }

        std::vector<BoundLiteral> bound_row;
        bound_row.reserve(row.size());
        for (std::size_t index = 0; index < row.size(); ++index) {
            const auto* literal = std::get_if<Literal>(&row[index]);
            if (literal == nullptr) {
                bind_err("INSERT VALUES currently supports only literal expressions");
                return std::unexpected(Error::AnalyzerError);
            }

            auto bound_literal =
                bind_literal(*literal, &target_columns[index].type, target_columns[index].nullable);
            if (!bound_literal) {
                return std::unexpected(bound_literal.error());
            }
            bound_row.push_back(std::move(*bound_literal));
        }
        bound_rows.push_back(std::move(bound_row));
    }

    return BoundInsertStmt{
        .table = std::move(*table),
        .columns = std::move(target_columns),
        .rows = std::move(bound_rows),
    };
}

auto Binder::bind_select(const SelectStmt& stmt) -> Result<BoundSelectStmt> {
    auto table = bind_table(stmt.table_name);
    if (!table) {
        return std::unexpected(table.error());
    }

    std::vector<BoundSelectItem> bound_items;
    bound_items.reserve(stmt.items.size());
    for (const auto& item : stmt.items) {
        if (std::holds_alternative<StarItem>(item)) {
            bound_items.emplace_back(BoundStarItem{});
            continue;
        }

        const auto& expr_item = std::get<ExprItem>(item);
        auto bound_expr = bind_expr(expr_item.expr, *table);
        if (!bound_expr) {
            return std::unexpected(bound_expr.error());
        }
        bound_items.emplace_back(BoundExprItem{
            .expr = std::move(*bound_expr),
            .alias = expr_item.alias,
        });
    }

    std::optional<BoundExpr> bound_where;
    if (stmt.where.has_value()) {
        auto where_expr = bind_expr(*stmt.where, *table);
        if (!where_expr) {
            return std::unexpected(where_expr.error());
        }

        const auto bool_type = lookup_type("bool");
        if (!bool_type) {
            return std::unexpected(bool_type.error());
        }
        if (!is_bool_expr(*where_expr, *bool_type)) {
            bind_err("WHERE expression must have type bool");
            return std::unexpected(Error::AnalyzerError);
        }
        bound_where = std::move(*where_expr);
    }

    return BoundSelectStmt{
        .table = std::move(*table),
        .items = std::move(bound_items),
        .where = std::move(bound_where),
    };
}

auto Binder::bind_table(std::string_view table_name) -> Result<BoundTableRef> {
    if (catalog == nullptr) {
        bind_err("binder has no catalog");
        return std::unexpected(Error::InvalidArgument);
    }

    auto catalog_class = catalog->get_class(table_name);
    if (!catalog_class) {
        if (catalog_class.error() == Error::NotFound) {
            bind_err(std::format("unknown table '{}'", table_name));
            return std::unexpected(Error::AnalyzerError);
        }
        return std::unexpected(catalog_class.error());
    }

    auto opened_table = catalog->open_table(table_name);
    if (!opened_table) {
        if (opened_table.error() == Error::NotFound) {
            bind_err(std::format("unknown table '{}'", table_name));
            return std::unexpected(Error::AnalyzerError);
        }
        return std::unexpected(opened_table.error());
    }

    const auto& schema = (*opened_table)->schema();
    std::vector<BoundColumnRef> columns;
    columns.reserve(schema.columns.size());
    for (std::size_t index = 0; index < schema.columns.size(); ++index) {
        auto type = lookup_type(schema.columns[index].type_oid);
        if (!type) {
            return std::unexpected(type.error());
        }

        columns.emplace_back(BoundColumnRef{
            .relation_oid = catalog_class->oid,
            .attnum = make_attnum(index),
            .name = schema.columns[index].name,
            .type = std::move(*type),
            .nullable = schema.columns[index].nullable,
        });
    }

    return BoundTableRef{
        .relation_oid = catalog_class->oid,
        .name = schema.name,
        .columns = std::move(columns),
    };
}

auto Binder::bind_column_ref(const ColumnRef& ref, const BoundTableRef& table)
    -> Result<BoundColumnRef> {
    const auto wanted = normalized_column_name(ref.name);
    for (const auto& column : table.columns) {
        if (normalized_column_name(column.name) == wanted) {
            return column;
        }
    }

    bind_err(std::format("unknown column '{}' on table '{}'", ref.name, table.name));
    return std::unexpected(Error::AnalyzerError);
}

auto Binder::infer_literal_type(const Literal& literal) -> Result<BoundTypeRef> {
    if (std::holds_alternative<NullLiteral>(literal)) {
        bind_err("NULL literal requires a target type");
        return std::unexpected(Error::AnalyzerError);
    }

    if (std::holds_alternative<IntLiteral>(literal)) {
        return lookup_type("int64");
    }
    if (std::holds_alternative<FloatLiteral>(literal)) {
        return lookup_type("float64");
    }
    if (std::holds_alternative<StrLiteral>(literal)) {
        return lookup_type("text");
    }
    return lookup_type("bool");
}

auto Binder::coerce_literal_to_type(const Literal& literal, const BoundTypeRef& target_type,
                                    b8 target_nullable) -> Result<BoundLiteral> {
    if (std::holds_alternative<NullLiteral>(literal)) {
        if (!static_cast<bool>(target_nullable)) {
            bind_err(
                std::format("NULL is not allowed for non-nullable type '{}'", target_type.name));
            return std::unexpected(Error::AnalyzerError);
        }
        return BoundLiteral{
            .type = target_type,
            .value = Value{.type_oid = target_type.oid, .bytes = {}, .is_null = b8{true}},
        };
    }

    if (types == nullptr) {
        bind_err("binder has no type registry");
        return std::unexpected(Error::InvalidArgument);
    }

    auto type = types->lookup(target_type.oid);
    if (!type) {
        return std::unexpected(type.error() == Error::NotFound ? Error::TypeNotFound
                                                               : type.error());
    }

    const auto text = literal_text(literal);
    auto bytes = (*type)->from_text(*text);
    if (!bytes) {
        bind_err(std::format("cannot coerce literal '{}' to type '{}'", *text, target_type.name));
        return std::unexpected(Error::AnalyzerError);
    }

    return BoundLiteral{
        .type = target_type,
        .value =
            Value{.type_oid = target_type.oid, .bytes = std::move(*bytes), .is_null = b8{false}},
    };
}

auto Binder::bind_literal(const Literal& literal, const BoundTypeRef* target_type,
                          b8 target_nullable) -> Result<BoundLiteral> {
    BoundTypeRef inferred_type{};
    if (target_type == nullptr) {
        auto inferred = infer_literal_type(literal);
        if (!inferred) {
            return std::unexpected(inferred.error());
        }
        inferred_type = std::move(*inferred);
        target_type = &inferred_type;
    }

    return coerce_literal_to_type(literal, *target_type, target_nullable);
}

auto Binder::bind_expr(const Expr& expr, const BoundTableRef& table,
                       const BoundTypeRef* target_type) -> Result<BoundExpr> {
    if (const auto* literal = std::get_if<Literal>(&expr); literal != nullptr) {
        auto bound_literal = bind_literal(*literal, target_type, b8{true});
        if (!bound_literal) {
            return std::unexpected(bound_literal.error());
        }
        return BoundExpr{std::move(*bound_literal)};
    }

    if (const auto* column = std::get_if<ColumnRef>(&expr); column != nullptr) {
        auto bound_column = bind_column_ref(*column, table);
        if (!bound_column) {
            return std::unexpected(bound_column.error());
        }
        return BoundExpr{std::move(*bound_column)};
    }

    const auto* binary = std::get_if<std::unique_ptr<BinaryExpr>>(&expr);
    if (binary == nullptr || *binary == nullptr) {
        bind_err("unsupported expression form");
        return std::unexpected(Error::AnalyzerError);
    }
    return bind_binary_expr(**binary, table);
}

auto Binder::bind_binary_expr(const BinaryExpr& expr, const BoundTableRef& table)
    -> Result<BoundExpr> {
    const auto bool_type = lookup_type("bool");
    if (!bool_type) {
        return std::unexpected(bool_type.error());
    }

    auto left = bind_expr(expr.left, table);
    if (!left) {
        return std::unexpected(left.error());
    }
    auto right = bind_expr(expr.right, table);
    if (!right) {
        return std::unexpected(right.error());
    }

    if (expr.op == BinaryOp::And || expr.op == BinaryOp::Or) {
        if (!is_bool_expr(*left, *bool_type) || !is_bool_expr(*right, *bool_type)) {
            bind_err("logical expressions require bool operands");
            return std::unexpected(Error::AnalyzerError);
        }

        return BoundExpr{std::make_unique<BoundBinaryExpr>(BoundBinaryExpr{
            .op = expr.op,
            .left = std::move(*left),
            .right = std::move(*right),
            .type = *bool_type,
        })};
    }

    if (bound_expr_type(*left).oid != bound_expr_type(*right).oid) {
        if (std::holds_alternative<Literal>(expr.left)) {
            left = bind_expr(expr.left, table, &bound_expr_type(*right));
            if (!left) {
                return std::unexpected(left.error());
            }
        } else if (std::holds_alternative<Literal>(expr.right)) {
            right = bind_expr(expr.right, table, &bound_expr_type(*left));
            if (!right) {
                return std::unexpected(right.error());
            }
        }
    }

    if (bound_expr_type(*left).oid != bound_expr_type(*right).oid) {
        bind_err(std::format("type mismatch in comparison: '{}' vs '{}'",
                             bound_expr_type(*left).name, bound_expr_type(*right).name));
        return std::unexpected(Error::AnalyzerError);
    }

    return BoundExpr{std::make_unique<BoundBinaryExpr>(BoundBinaryExpr{
        .op = expr.op,
        .left = std::move(*left),
        .right = std::move(*right),
        .type = *bool_type,
    })};
}

auto Binder::lookup_type(std::string_view name) const -> Result<BoundTypeRef> {
    if (catalog == nullptr) {
        return std::unexpected(Error::InvalidArgument);
    }

    auto type = catalog->get_type(name);
    if (!type) {
        return std::unexpected(type.error() == Error::NotFound ? Error::TypeNotFound
                                                               : type.error());
    }
    return BoundTypeRef{.oid = type->oid, .name = type->name, .param = std::nullopt};
}

auto Binder::lookup_type(u32 oid) const -> Result<BoundTypeRef> {
    if (types == nullptr) {
        return std::unexpected(Error::InvalidArgument);
    }

    auto type = types->lookup(oid);
    if (!type) {
        return std::unexpected(type.error() == Error::NotFound ? Error::TypeNotFound
                                                               : type.error());
    }
    return BoundTypeRef{.oid = (*type)->oid, .name = (*type)->name, .param = std::nullopt};
}

auto Binder::bind_err(std::string msg) -> void {
    last_error = std::move(msg);
}

}  // namespace edb
