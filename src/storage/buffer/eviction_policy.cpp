// src/storage/buffer/eviction_policy.cpp

#include "storage/buffer/eviction_policy.hpp"

#include <memory>

#include "storage/buffer/arc_policy.hpp"
#include "storage/buffer/clock_sweep_policy.hpp"
#include "storage/buffer/lru_k_policy.hpp"

namespace edb {

auto make_eviction_policy(const EvictionPolicyConfig& config) -> std::unique_ptr<EvictionPolicy> {
    switch (config.kind) {
        case EvictionPolicyKind::ClockSweep:
            return std::make_unique<ClockSweepPolicy>();
        case EvictionPolicyKind::LruK:
            return std::make_unique<LruKPolicy>(config.lru_k_history);
        case EvictionPolicyKind::Arc:
            return std::make_unique<ArcPolicy>();
    }
    return std::make_unique<ClockSweepPolicy>();
}

}  // namespace edb