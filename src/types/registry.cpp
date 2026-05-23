// src/types/registry.cpp

#include "types/registry.hpp"

#include <expected>
#include <utility>

namespace edb {

auto TypeRegistry::lookup(std::string_view name) const -> Result<const Type*> {
    const auto found = by_name.find(std::string{name});
    if (found == by_name.end()) {
        return std::unexpected(Error::NotFound);
    }
    return found->second;
}

auto TypeRegistry::lookup(u32 oid) const -> Result<const Type*> {
    const auto found = by_oid.find(oid);
    if (found == by_oid.end()) {
        return std::unexpected(Error::NotFound);
    }
    return found->second;
}

auto TypeRegistry::size() const -> usize {
    return usize{types.size()};
}

auto TypeRegistry::register_type_impl(Type type) -> VoidResult {
    if (type.name.empty()) {
        return std::unexpected(Error::InvalidArgument);
    }
    if (by_name.contains(type.name)) {
        return std::unexpected(Error::AlreadyExists);
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