#pragma once

// src/query/binder.hpp
//
// Binder -- resolves parser AST nodes against catalog metadata and type names.
//
// Phase 5b starts with CREATE TABLE binding only. The public BoundStmt variant is
// intentionally small so later SELECT/INSERT binding can extend it without
// reshaping the entry point.

#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "catalog/catalog.hpp"
#include "query/ast.hpp"
#include "utils/error.hpp"

namespace edb {

struct BoundTypeRef {
    u32                oid{0};
    std::string        name;
    std::optional<u32> param;
};

struct BoundColumnDef {
    std::string  name;
    BoundTypeRef type;
    b8           nullable{b8{true}};
    b8           primary_key{b8{false}};
    b8           unique_constraint{b8{false}};
};

struct BoundCreateTableStmt {
    std::string                 table_name;
    std::vector<BoundColumnDef> columns;
    b8                          if_not_exists{b8{false}};
};

using BoundStmt = std::variant<BoundCreateTableStmt>;

class Binder {
   public:
    explicit Binder(Catalog& catalog) noexcept;

    [[nodiscard]] auto bind(const Stmt& stmt) -> Result<BoundStmt>;
    [[nodiscard]] auto error_message() const noexcept -> std::string_view;

   private:
    [[nodiscard]] auto bind_create_table(const CreateTableStmt& stmt)
        -> Result<BoundCreateTableStmt>;
    auto bind_err(std::string msg) -> void;

    Catalog*  catalog{nullptr};
    std::string  last_error;
};

}  // namespace edb
