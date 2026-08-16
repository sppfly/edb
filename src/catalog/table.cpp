// src/catalog/table.cpp

#include "catalog/table.hpp"

#include <expected>

namespace edb {

Table::Table(const TypeRegistry& registry, StorageEngine& engine, TableSchema schema)
    : storage{&engine},
      table_schema{std::move(schema)},
      row_codec{registry, table_schema.columns} {}

auto Table::insert(std::span<const Value> values) -> Result<TupleId> {
    if (storage == nullptr) {
        return std::unexpected(Error::InvalidArgument);
    }

    auto encoded = row_codec.encode(values);
    if (!encoded) {
        return std::unexpected(encoded.error());
    }
    return storage->insert(*encoded);
}

auto Table::scan_rows() -> Result<std::vector<TableRow>> {
    if (storage == nullptr) {
        return std::unexpected(Error::InvalidArgument);
    }

    auto handle = storage->begin_scan();
    if (!handle) {
        return std::unexpected(handle.error());
    }

    std::vector<TableRow> rows;
    while (true) {
        auto next = storage->scan_next(*handle);
        if (!next) {
            return std::unexpected(next.error());
        }
        if (!next->has_value()) {
            break;
        }

        auto decoded = row_codec.decode((*next)->data);
        if (!decoded) {
            return std::unexpected(decoded.error());
        }
        rows.push_back(TableRow{.id = (*next)->id, .values = std::move(*decoded)});
    }

    auto status = storage->end_scan(*handle);
    if (!status) {
        return std::unexpected(status.error());
    }
    return rows;
}

auto Table::scan() -> Result<std::vector<std::vector<Value>>> {
    auto rows = scan_rows();
    if (!rows) {
        return std::unexpected(rows.error());
    }

    std::vector<std::vector<Value>> values;
    values.reserve(rows->size());
    for (auto& row : *rows) {
        values.push_back(std::move(row.values));
    }
    return values;
}

auto Table::schema() const -> const TableSchema& {
    return table_schema;
}

}  // namespace edb