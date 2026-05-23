#pragma once

// src/types/builtin_types.hpp

#include "types/registry.hpp"

namespace edb {

auto register_builtin_types(TypeRegistry& registry) -> VoidResult;

}  // namespace edb