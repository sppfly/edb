// src/query/binder.cpp

#include "query/binder.hpp"

#include <algorithm>
#include <format>
#include <string>
#include <string_view>
#include <unordered_set>

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

}  // namespace

Binder::Binder(EdbCatalog& catalog_in) noexcept : catalog{&catalog_in} {}

auto Binder::bind(const Stmt& stmt) -> EdbResult<BoundStmt> {
    if (const auto* create_stmt = std::get_if<CreateTableStmt>(&stmt); create_stmt != nullptr) {
        auto bound = bind_create_table(*create_stmt);
        if (!bound) {
            return std::unexpected(bound.error());
        }
        return BoundStmt{std::move(*bound)};
    }

    bind_err("binding for this statement kind is not implemented yet");
    return std::unexpected(EdbError::NotSupported);
}

auto Binder::error_message() const noexcept -> std::string_view { return last_error; }

auto Binder::bind_create_table(const CreateTableStmt& stmt) -> EdbResult<BoundCreateTableStmt> {
    if (catalog == nullptr) {
        bind_err("binder has no catalog");
        return std::unexpected(EdbError::InvalidArgument);
    }

    std::unordered_set<std::string> seen_columns;
    std::vector<BoundColumnDef> columns;
    columns.reserve(stmt.columns.size());

    for (const auto& column : stmt.columns) {
        const auto dedup_name = normalized_column_name(column.name);
        if (seen_columns.contains(dedup_name)) {
            bind_err(std::format("duplicate column '{}' in CREATE TABLE '{}'", column.name,
                                 stmt.table_name));
            return std::unexpected(EdbError::AnalyzerError);
        }
        seen_columns.emplace(dedup_name);

        const auto type_name = canonical_type_name(column.type);
        auto type = catalog->get_type(type_name);
        if (!type) {
            bind_err(std::format("unknown type '{}' for column '{}'", column.type.name,
                                 column.name));
            return std::unexpected(type.error() == EdbError::NotFound ? EdbError::TypeNotFound
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

auto Binder::bind_err(std::string msg) -> void { last_error = std::move(msg); }

}  // namespace edb
