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

struct EdbTableSchema {
    u32 relation_oid{0};
    std::string name;
    std::vector<EdbColumnSchema> columns;
};

struct EdbTableRow {
    EdbTupleId id;
    std::vector<EdbValue> values;
};

class EdbTable {
   public:
    EdbTable(const EdbTypeRegistry& registry, EdbStorageEngineOps& engine, EdbTableSchema schema);

    EdbTable(const EdbTable&) = delete;
    EdbTable& operator=(const EdbTable&) = delete;
    EdbTable(EdbTable&&) = delete;
    EdbTable& operator=(EdbTable&&) = delete;
    ~EdbTable() = default;

    [[nodiscard]] auto insert(std::span<const EdbValue> values) -> EdbResult<EdbTupleId>;
    [[nodiscard]] auto scan_rows() -> EdbResult<std::vector<EdbTableRow>>;
    [[nodiscard]] auto scan() -> EdbResult<std::vector<std::vector<EdbValue>>>;

    [[nodiscard]] auto schema() const -> const EdbTableSchema&;

   private:
    EdbStorageEngineOps* storage{nullptr};
    EdbTableSchema table_schema;
    EdbRowCodec row_codec;
};

}  // namespace edb