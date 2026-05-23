#pragma once

// src/types/row_codec.hpp

#include <span>
#include <string>
#include <vector>

#include "types/registry.hpp"
#include "utils/error.hpp"
#include "utils/primitives.hpp"

namespace edb {

struct EdbValue {
    u32 type_oid{0};
    std::vector<std::byte> bytes;
    b8 is_null{false};
};

struct EdbColumnSchema {
    std::string name;
    u32 type_oid{0};
    b8 nullable{false};
};

class EdbRowCodec {
   public:
    EdbRowCodec(const EdbTypeRegistry& registry, std::vector<EdbColumnSchema> schema);

    EdbRowCodec(const EdbRowCodec&) = delete;
    EdbRowCodec& operator=(const EdbRowCodec&) = delete;
    EdbRowCodec(EdbRowCodec&&) = delete;
    EdbRowCodec& operator=(EdbRowCodec&&) = delete;
    ~EdbRowCodec() = default;

    [[nodiscard]] auto encode(std::span<const EdbValue> values) const
        -> EdbResult<std::vector<std::byte>>;
    [[nodiscard]] auto decode(std::span<const std::byte> tuple) const
        -> EdbResult<std::vector<EdbValue>>;

    [[nodiscard]] auto columns() const -> std::span<const EdbColumnSchema>;

   private:
    [[nodiscard]] auto lookup_type(u32 oid) const -> EdbResult<const EdbType*>;

    const EdbTypeRegistry* registry{nullptr};
    std::vector<EdbColumnSchema> schema;
};

}  // namespace edb