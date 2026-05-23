#pragma once

// src/types/registry.hpp

#include <deque>
#include <string>
#include <string_view>
#include <unordered_map>

#include "types/type_ops.hpp"
#include "utils/contracts.hpp"
#include "utils/error.hpp"
#include "utils/primitives.hpp"

namespace edb {

class EdbTypeRegistry {
   public:
    EdbTypeRegistry() = default;

    EdbTypeRegistry(const EdbTypeRegistry&) = delete;
    EdbTypeRegistry& operator=(const EdbTypeRegistry&) = delete;
    EdbTypeRegistry(EdbTypeRegistry&&) = delete;
    EdbTypeRegistry& operator=(EdbTypeRegistry&&) = delete;
    ~EdbTypeRegistry() = default;

    template <EdbTypeImpl T>
    auto register_type(std::string_view name) -> EdbStatus EDB_PRE(!name.empty()) {
        EdbType type{};
        type.name = std::string{name};
        type.fixed_size = T::fixed_size();
        type.from_text = [](std::string_view text) { return T::from_text(text); };
        type.to_text = [](std::span<const std::byte> bytes) { return T::to_text(bytes); };
        type.compare = [](std::span<const std::byte> lhs, std::span<const std::byte> rhs) {
            return T::compare(lhs, rhs);
        };
        type.hash = [](std::span<const std::byte> bytes) { return T::hash(bytes); };
        return register_type_impl(std::move(type));
    }

    auto lookup(std::string_view name) const -> EdbResult<const EdbType*> EDB_PRE(!name.empty());
    auto lookup(u32 oid) const -> EdbResult<const EdbType*> EDB_PRE(oid > u32{0});
    [[nodiscard]] auto size() const -> usize;

   private:
    auto register_type_impl(EdbType type) -> EdbStatus;

    std::deque<EdbType> types;
    std::unordered_map<std::string, const EdbType*> by_name;
    std::unordered_map<u32, const EdbType*> by_oid;
    u32 next_oid{1};
};

}  // namespace edb