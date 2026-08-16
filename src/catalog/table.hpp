#pragma once

// src/catalog/table.hpp

#include <span>
#include <string>
#include <vector>

#include "storage/engine/engine_ops.hpp"
#include "types/row_codec.hpp"
#include "utils/error.hpp"
#include "utils/primitives.hpp"

namespace edb {

struct TableSchema {
    u32 relation_oid{0};
    std::string name;
    std::vector<ColumnSchema> columns;
};

struct TableRow {
    TupleId id;
    std::vector<Value> values;
};

class Table {
   public:
    Table(const TypeRegistry& registry, StorageEngineOps& engine, TableSchema schema);

    Table(const Table&) = delete;
    Table& operator=(const Table&) = delete;
    Table(Table&&) = delete;
    Table& operator=(Table&&) = delete;
    ~Table() = default;

    [[nodiscard]] auto insert(std::span<const Value> values) -> Result<TupleId>;
    [[nodiscard]] auto scan_rows() -> Result<std::vector<TableRow>>;
    [[nodiscard]] auto scan() -> Result<std::vector<std::vector<Value>>>;

    [[nodiscard]] auto schema() const -> const TableSchema&;

   private:
    StorageEngineOps* storage{nullptr};
    TableSchema table_schema;
    RowCodec row_codec;
};

}  // namespace edb