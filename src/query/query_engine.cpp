// src/query/query_engine.cpp

#include "query/query_engine.hpp"

#include <utility>

#include "query/binder.hpp"
#include "query/logical_plan.hpp"
#include "query/parser.hpp"
#include "query/physical_plan.hpp"

namespace edb {

QueryEngine::QueryEngine(Catalog& catalog, const TypeRegistry& types) noexcept
    : catalog{&catalog}, types{&types} {}

auto QueryEngine::execute(std::string_view sql) -> Result<QueryResult> {
    if (catalog == nullptr || types == nullptr) {
        return query_err("query engine is not initialized", Error::InvalidArgument);
    }

    Parser parser{sql};
    auto parsed = parser.parse();
    if (!parsed) {
        return query_err(parser.error_message(), parsed.error());
    }
    if (parsed->size() != std::size_t{1}) {
        return query_err("execute expects exactly one SQL statement", Error::InvalidArgument);
    }

    Binder binder{*catalog, *types};
    auto bound = binder.bind(parsed->front());
    if (!bound) {
        return query_err(binder.error_message(), bound.error());
    }

    LogicalPlanner logical_planner;
    auto logical = logical_planner.build(std::move(*bound));
    if (!logical) {
        return query_err(logical_planner.error_message(), logical.error());
    }

    PhysicalPlanner physical_planner;
    auto physical = physical_planner.build(std::move(*logical));
    if (!physical) {
        return query_err(physical_planner.error_message(), physical.error());
    }

    ExecBuilder exec_builder{*catalog, *types};
    auto exec = exec_builder.build(std::move(*physical));
    if (!exec) {
        return query_err(exec_builder.error_message(), exec.error());
    }

    auto opened = (*exec)->open();
    if (!opened) {
        return query_err("executor open failed", opened.error());
    }

    QueryResult result;
    while (true) {
        ExecRow row;
        auto next = (*exec)->next(row);
        if (!next) {
            const auto error = next.error();
            auto closed_after_error = (*exec)->close();
            if (!closed_after_error) {
                return query_err("executor close after next failure failed",
                                 closed_after_error.error());
            }
            return query_err("executor next failed", error);
        }
        if (!static_cast<bool>(*next)) {
            break;
        }
        result.rows.push_back(std::move(row));
    }

    auto closed = (*exec)->close();
    if (!closed) {
        return query_err("executor close failed", closed.error());
    }

    return result;
}

auto QueryEngine::error_message() const noexcept -> std::string_view {
    return last_error;
}

auto QueryEngine::query_err(std::string_view msg, Error error) -> Result<QueryResult> {
    last_error = std::string{msg};
    return std::unexpected(error);
}

}  // namespace edb