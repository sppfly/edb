// src/utils/contracts.cpp
//
// GCC's experimental C++26 contracts implementation emits calls to a global
// handle_contract_violation function when -fcontracts is enabled. Keep the
// handler tiny and dependency-free so every target can link it through
// edb_utils.

#include <exception>

namespace std::contracts {
class contract_violation;
}  // namespace std::contracts

void handle_contract_violation(const std::contracts::contract_violation& violation) {
    (void)violation;
    std::terminate();
}
