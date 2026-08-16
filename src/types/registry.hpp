#pragma once

// src/types/registry.hpp

#include <deque>
#include <string>
#include <string_view>
#include <unordered_map>

#include "types/type_ops.hpp"
#include "utils/assert.hpp"
#include "utils/error.hpp"
#include "utils/primitives.hpp"

namespace edb {

class TypeRegistry {
public:
    TypeRegistry() = default;

    TypeRegistry(const TypeRegistry&) = delete;
    TypeRegistry& operator=(const TypeRegistry&) = delete;
    TypeRegistry(TypeRegistry&&) = delete;
    TypeRegistry& operator=(TypeRegistry&&) = delete;
    ~TypeRegistry() = default;

    template <TypeImpl T>
    auto register_type(std::string_view name) -> VoidResult {
        EDB_ASSERT(!name.empty());
        Type type{};
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

    auto lookup(std::string_view name) const -> Result<const Type*>;
    auto lookup(u32 oid) const -> Result<const Type*>;
    [[nodiscard]] auto size() const -> usize;

private:
    auto register_type_impl(Type type) -> VoidResult;

    std::deque<Type> types;
    std::unordered_map<std::string, const Type*> by_name;
    std::unordered_map<u32, const Type*> by_oid;
    u32 next_oid{1};
};

}  // namespace edb