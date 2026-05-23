// src/types/registry.cpp

#include "types/registry.hpp"

#include <expected>
#include <utility>

namespace edb {

auto EdbTypeRegistry::lookup(std::string_view name) const -> EdbResult<const EdbType*> {
    const auto found = by_name.find(std::string{name});
    if (found == by_name.end()) {
        return std::unexpected(EdbError::NotFound);
    }
    return found->second;
}

auto EdbTypeRegistry::lookup(u32 oid) const -> EdbResult<const EdbType*> {
    const auto found = by_oid.find(oid);
    if (found == by_oid.end()) {
        return std::unexpected(EdbError::NotFound);
    }
    return found->second;
}

auto EdbTypeRegistry::size() const -> usize {
    return usize{types.size()};
}

auto EdbTypeRegistry::register_type_impl(EdbType type) -> EdbStatus {
    if (type.name.empty()) {
        return std::unexpected(EdbError::InvalidArgument);
    }
    if (by_name.contains(type.name)) {
        return std::unexpected(EdbError::AlreadyExists);
    }

    type.oid = next_oid;
    ++next_oid;
    types.push_back(std::move(type));

    const auto* stored = &types.back();
    by_name.emplace(stored->name, stored);
    by_oid.emplace(stored->oid, stored);
    return {};
}

}  // namespace edb