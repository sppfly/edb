// src/catalog/table.cpp

#include "catalog/table.hpp"

#include <expected>

namespace edb {

EdbTable::EdbTable(const EdbTypeRegistry& registry, EdbStorageEngineOps& engine,
                   EdbTableSchema schema)
    : storage{&engine},
      table_schema{std::move(schema)},
      row_codec{registry, table_schema.columns} {}

auto EdbTable::insert(std::span<const EdbValue> values) -> EdbResult<EdbTupleId> {
    if (storage == nullptr) {
        return std::unexpected(EdbError::InvalidArgument);
    }

    auto encoded = row_codec.encode(values);
    if (!encoded) {
        return std::unexpected(encoded.error());
    }
    return storage->insert(*encoded);
}

auto EdbTable::scan_rows() -> EdbResult<std::vector<EdbTableRow>> {
    if (storage == nullptr) {
        return std::unexpected(EdbError::InvalidArgument);
    }

    auto handle = storage->begin_scan();
    if (!handle) {
        return std::unexpected(handle.error());
    }

    std::vector<EdbTableRow> rows;
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
        rows.push_back(EdbTableRow{.id = (*next)->id, .values = std::move(*decoded)});
    }

    auto status = storage->end_scan(*handle);
    if (!status) {
        return std::unexpected(status.error());
    }
    return rows;
}

auto EdbTable::scan() -> EdbResult<std::vector<std::vector<EdbValue>>> {
    auto rows = scan_rows();
    if (!rows) {
        return std::unexpected(rows.error());
    }

    std::vector<std::vector<EdbValue>> values;
    values.reserve(rows->size());
    for (auto& row : *rows) {
        values.push_back(std::move(row.values));
    }
    return values;
}

auto EdbTable::schema() const -> const EdbTableSchema& {
    return table_schema;
}

}  // namespace edb