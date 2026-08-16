// tests/unit/storage/buffer/eviction_policy_test.cpp

#include "storage/buffer/eviction_policy.hpp"

#include <gtest/gtest.h>

#include <array>
#include <memory>

#include "storage/buffer/arc_policy.hpp"
#include "storage/buffer/clock_sweep_policy.hpp"
#include "storage/buffer/lru_k_policy.hpp"

using namespace edb;

TEST(EdbEvictionPolicy, ClockSweepGivesReferencedPagesSecondChance) {
    ClockSweepPolicy policy;
    ASSERT_TRUE(policy.reset(usize{2}).has_value());
    ASSERT_TRUE(policy.record_load(u64{10}, usize{0}).has_value());
    ASSERT_TRUE(policy.record_load(u64{11}, usize{1}).has_value());

    const std::array frames{
        EvictionFrameState{.page_id = u64{10}, .valid = b8{true}, .pinned = b8{false}},
        EvictionFrameState{.page_id = u64{11}, .valid = b8{true}, .pinned = b8{false}}};

    auto victim = policy.choose_victim(frames);

    ASSERT_TRUE(victim.has_value());
    EXPECT_EQ(victim->value, usize{0}.value);
}

TEST(EdbEvictionPolicy, LruKEvictsPagesWithShortestHistoryBeforeStablePages) {
    LruKPolicy policy{usize{2}};
    ASSERT_TRUE(policy.reset(usize{3}).has_value());
    ASSERT_TRUE(policy.record_load(u64{10}, usize{0}).has_value());
    ASSERT_TRUE(policy.record_load(u64{11}, usize{1}).has_value());
    ASSERT_TRUE(policy.record_load(u64{12}, usize{2}).has_value());
    ASSERT_TRUE(policy.record_access(u64{10}, usize{0}).has_value());
    ASSERT_TRUE(policy.record_access(u64{11}, usize{1}).has_value());
    ASSERT_TRUE(policy.record_access(u64{10}, usize{0}).has_value());

    const std::array frames{
        EvictionFrameState{.page_id = u64{10}, .valid = b8{true}, .pinned = b8{false}},
        EvictionFrameState{.page_id = u64{11}, .valid = b8{true}, .pinned = b8{false}},
        EvictionFrameState{.page_id = u64{12}, .valid = b8{true}, .pinned = b8{false}}};

    auto victim = policy.choose_victim(frames);

    ASSERT_TRUE(victim.has_value());
    EXPECT_EQ(victim->value, usize{2}.value);
}

TEST(EdbEvictionPolicy, LruKUsesKthMostRecentAccessForStablePages) {
    LruKPolicy policy{usize{2}};
    ASSERT_TRUE(policy.reset(usize{3}).has_value());
    ASSERT_TRUE(policy.record_load(u64{10}, usize{0}).has_value());
    ASSERT_TRUE(policy.record_load(u64{11}, usize{1}).has_value());
    ASSERT_TRUE(policy.record_load(u64{12}, usize{2}).has_value());
    ASSERT_TRUE(policy.record_access(u64{10}, usize{0}).has_value());
    ASSERT_TRUE(policy.record_access(u64{11}, usize{1}).has_value());
    ASSERT_TRUE(policy.record_access(u64{10}, usize{0}).has_value());
    ASSERT_TRUE(policy.record_access(u64{12}, usize{2}).has_value());

    const std::array frames{
        EvictionFrameState{.page_id = u64{10}, .valid = b8{true}, .pinned = b8{false}},
        EvictionFrameState{.page_id = u64{11}, .valid = b8{true}, .pinned = b8{false}},
        EvictionFrameState{.page_id = u64{12}, .valid = b8{true}, .pinned = b8{false}}};

    auto victim = policy.choose_victim(frames);

    ASSERT_TRUE(victim.has_value());
    EXPECT_EQ(victim->value, usize{1}.value);
}

TEST(EdbEvictionPolicy, ArcPromotesReusedPagesAndAdaptsOnRecentGhostHit) {
    ArcPolicy policy;
    ASSERT_TRUE(policy.reset(usize{2}).has_value());
    ASSERT_TRUE(policy.record_load(u64{10}, usize{0}).has_value());
    ASSERT_TRUE(policy.record_load(u64{11}, usize{1}).has_value());
    ASSERT_TRUE(policy.record_access(u64{10}, usize{0}).has_value());

    const std::array first_frames{
        EvictionFrameState{.page_id = u64{10}, .valid = b8{true}, .pinned = b8{false}},
        EvictionFrameState{.page_id = u64{11}, .valid = b8{true}, .pinned = b8{false}}};

    auto first_victim = policy.choose_victim(first_frames);
    ASSERT_TRUE(first_victim.has_value());
    EXPECT_EQ(first_victim->value, usize{1}.value);

    ASSERT_TRUE(policy.record_evict(u64{11}, usize{1}).has_value());
    ASSERT_TRUE(policy.record_miss(u64{12}).has_value());
    ASSERT_TRUE(policy.record_load(u64{12}, usize{1}).has_value());
    ASSERT_TRUE(policy.record_miss(u64{11}).has_value());

    const std::array second_frames{
        EvictionFrameState{.page_id = u64{10}, .valid = b8{true}, .pinned = b8{false}},
        EvictionFrameState{.page_id = u64{12}, .valid = b8{true}, .pinned = b8{false}}};

    auto second_victim = policy.choose_victim(second_frames);
    ASSERT_TRUE(second_victim.has_value());
    EXPECT_EQ(second_victim->value, usize{0}.value);
}

TEST(EdbEvictionPolicy, FactoryCreatesConfiguredPolicy) {
    auto policy = make_eviction_policy(
        EvictionPolicyConfig{.kind = EvictionPolicyKind::LruK, .lru_k_history = usize{2}});

    ASSERT_NE(policy, nullptr);
    EXPECT_TRUE(policy->reset(usize{1}).has_value());
}