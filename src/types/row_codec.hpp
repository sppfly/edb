#pragma once

// src/types/row_codec.hpp

#include <span>
#include <string>
#include <vector>

#include "types/registry.hpp"
#include "utils/error.hpp"
#include "utils/primitives.hpp"

namespace edb {

struct Value {
    u32 type_oid{0};
    std::vector<std::byte> bytes;
    b8 is_null{false};
};

struct ColumnSchema {
    std::string name;
    u32 type_oid{0};
    b8 nullable{false};
};

class RowCodec {
   public:
    RowCodec(const TypeRegistry& registry, std::vector<ColumnSchema> schema);

    RowCodec(const RowCodec&) = delete;
    RowCodec& operator=(const RowCodec&) = delete;
    RowCodec(RowCodec&&) = delete;
    RowCodec& operator=(RowCodec&&) = delete;
    ~RowCodec() = default;

    [[nodiscard]] auto encode(std::span<const Value> values) const
        -> Result<std::vector<std::byte>>;
    [[nodiscard]] auto decode(std::span<const std::byte> tuple) const -> Result<std::vector<Value>>;

    [[nodiscard]] auto columns() const -> std::span<const ColumnSchema>;

   private:
    [[nodiscard]] auto lookup_type(u32 oid) const -> Result<const Type*>;

    const TypeRegistry* registry{nullptr};
    std::vector<ColumnSchema> schema;
};

}  // namespace edb