#pragma once

// src/query/query_engine.hpp
//
// Thin SQL execution facade for the Phase 5 reference path.

#include <string>
#include <string_view>
#include <vector>

#include "catalog/catalog.hpp"
#include "query/exec.hpp"
#include "utils/error.hpp"

namespace edb {

struct QueryResult {
    std::vector<ExecRow> rows;
};

class QueryEngine {
public:
    QueryEngine(Catalog& catalog, const TypeRegistry& types) noexcept;

    [[nodiscard]] auto execute(std::string_view sql) -> Result<QueryResult>;
    [[nodiscard]] auto error_message() const noexcept -> std::string_view;

private:
    auto query_err(std::string_view msg, Error error) -> Result<QueryResult>;

    Catalog* catalog{nullptr};
    const TypeRegistry* types{nullptr};
    std::string last_error;
};

}  // namespace edb