// tests/unit/storage/buffer/buffer_pool_test.cpp

#include "storage/buffer/buffer_pool.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <span>
#include <vector>

using namespace edb;

class MockIOOps : public StorageIO {
public:
    std::vector<std::byte> storage;
    usize sync_calls{0};

private:
    auto open_impl(const char* /*path*/, const IOConfig& /*cfg*/) -> VoidResult override {
        return {};
    }
    auto close_impl() -> VoidResult override { return {}; }

    auto read_impl(u64 offset, std::span<std::byte> buf) -> Result<usize> override {
        const auto off = offset.value;
        if (off >= storage.size()) {
            return usize{0};
        }
        const auto available = storage.size() - off;
        const auto count = std::min(available, buf.size());
        auto src = std::span<const std::byte>{storage}.subspan(off, count);
        std::ranges::copy(src, buf.begin());
        return usize{count};
    }

    auto write_impl(u64 offset, std::span<const std::byte> buf) -> Result<usize> override {
        const auto off = offset.value;
        if ((off + buf.size()) > storage.size()) {
            storage.resize(off + buf.size());
        }
        auto dst = std::span<std::byte>{storage}.subspan(off, buf.size());
        std::ranges::copy(buf, dst.begin());
        return usize{buf.size()};
    }

    auto sync_impl() -> VoidResult override {
        ++sync_calls;
        return {};
    }
    auto datasync_impl() -> VoidResult override { return {}; }
    auto truncate_impl(u64 size) -> VoidResult override {
        storage.resize(size.value);
        return {};
    }
    auto file_size_impl() -> Result<u64> override { return u64{storage.size()}; }
};

class BufferPoolTest : public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(page_store.open(io, PageStoreConfig{.page_size = usize{64}}).has_value());
    }

    auto allocate_pages(usize count) -> void {
        for (usize page_index{0}; page_index < count; ++page_index) {
            ASSERT_TRUE(page_store.allocate_page().has_value());
        }
    }

    MockIOOps io;
    PageStore page_store;
};

TEST(EdbBufferPoolConfig, DefaultCapacity) {
    constexpr BufferPoolConfig cfg{};
    EXPECT_EQ(cfg.capacity_pages.value, usize{1024}.value);
    EXPECT_EQ(cfg.eviction.kind, EvictionPolicyKind::ClockSweep);
}

TEST_F(BufferPoolTest, OpenSetsCapacityAndPageSize) {
    BufferPool pool;

    ASSERT_TRUE(pool.open(page_store, BufferPoolConfig{.capacity_pages = usize{2}}).has_value());

    EXPECT_EQ(pool.capacity().value, usize{2}.value);
    EXPECT_EQ(pool.page_size().value, usize{64}.value);
}

TEST_F(BufferPoolTest, FetchReadsExistingPage) {
    allocate_pages(usize{1});
    std::array<std::byte, 64> page{};
    page[0] = std::byte{0xAB};
    ASSERT_TRUE(page_store.write_page(u64{0}, page).has_value());

    BufferPool pool;
    ASSERT_TRUE(pool.open(page_store, BufferPoolConfig{.capacity_pages = usize{1}}).has_value());

    auto handle = pool.fetch(u64{0});
    ASSERT_TRUE(handle.has_value());
    EXPECT_EQ(handle->data()[0], std::byte{0xAB});
    ASSERT_TRUE(pool.unpin(*handle, b8{false}).has_value());
}

TEST_F(BufferPoolTest, DirtyPageFlushesToStore) {
    allocate_pages(usize{1});
    BufferPool pool;
    ASSERT_TRUE(pool.open(page_store, BufferPoolConfig{.capacity_pages = usize{1}}).has_value());

    auto handle = pool.fetch(u64{0});
    ASSERT_TRUE(handle.has_value());
    handle->data()[3] = std::byte{0xCD};
    ASSERT_TRUE(pool.unpin(*handle, b8{true}).has_value());
    ASSERT_TRUE(pool.flush(u64{0}).has_value());

    std::array<std::byte, 64> persisted{};
    ASSERT_TRUE(page_store.read_page(u64{0}, persisted).has_value());
    EXPECT_EQ(persisted[3], std::byte{0xCD});
}

TEST_F(BufferPoolTest, EvictionWritesDirtyVictim) {
    allocate_pages(usize{2});
    BufferPool pool;
    ASSERT_TRUE(pool.open(page_store, BufferPoolConfig{.capacity_pages = usize{1}}).has_value());

    auto first = pool.fetch(u64{0});
    ASSERT_TRUE(first.has_value());
    first->data()[0] = std::byte{0x11};
    ASSERT_TRUE(pool.unpin(*first, b8{true}).has_value());

    auto second = pool.fetch(u64{1});
    ASSERT_TRUE(second.has_value());
    ASSERT_TRUE(pool.unpin(*second, b8{false}).has_value());

    std::array<std::byte, 64> persisted{};
    ASSERT_TRUE(page_store.read_page(u64{0}, persisted).has_value());
    EXPECT_EQ(persisted[0], std::byte{0x11});
}

TEST_F(BufferPoolTest, AllPinnedFramesReturnBufferPoolFull) {
    allocate_pages(usize{2});
    BufferPool pool;
    ASSERT_TRUE(pool.open(page_store, BufferPoolConfig{.capacity_pages = usize{1}}).has_value());

    auto pinned = pool.fetch(u64{0});
    ASSERT_TRUE(pinned.has_value());

    auto second = pool.fetch(u64{1});
    ASSERT_FALSE(second.has_value());
    EXPECT_EQ(second.error(), Error::BufferPoolFull);

    ASSERT_TRUE(pool.unpin(*pinned, b8{false}).has_value());
}

TEST_F(BufferPoolTest, FetchNewReturnsZeroedDirtyPage) {
    allocate_pages(usize{1});
    std::array<std::byte, 64> old_page{};
    old_page[0] = std::byte{0xEE};
    ASSERT_TRUE(page_store.write_page(u64{0}, old_page).has_value());

    BufferPool pool;
    ASSERT_TRUE(pool.open(page_store, BufferPoolConfig{.capacity_pages = usize{1}}).has_value());

    auto handle = pool.fetch_new(u64{0});
    ASSERT_TRUE(handle.has_value());
    EXPECT_TRUE(
        std::ranges::all_of(handle->data(), [](std::byte value) { return value == std::byte{0}; }));
    ASSERT_TRUE(pool.unpin(*handle, b8{true}).has_value());
}

TEST_F(BufferPoolTest, FetchWorksWithConfiguredLruKPolicy) {
    allocate_pages(usize{2});
    BufferPool pool;
    ASSERT_TRUE(pool.open(page_store, BufferPoolConfig{.capacity_pages = usize{1},
                                                       .eviction =
                                                           EvictionPolicyConfig{
                                                               .kind = EvictionPolicyKind::LruK,
                                                               .lru_k_history = usize{2},
                                                           }})
                    .has_value());

    auto first = pool.fetch(u64{0});
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(pool.unpin(*first, b8{false}).has_value());

    auto second = pool.fetch(u64{1});
    ASSERT_TRUE(second.has_value());
    ASSERT_TRUE(pool.unpin(*second, b8{false}).has_value());
}

TEST_F(BufferPoolTest, FetchWorksWithConfiguredArcPolicy) {
    allocate_pages(usize{2});
    BufferPool pool;
    ASSERT_TRUE(pool.open(page_store, BufferPoolConfig{.capacity_pages = usize{1},
                                                       .eviction =
                                                           EvictionPolicyConfig{
                                                               .kind = EvictionPolicyKind::Arc,
                                                           }})
                    .has_value());

    auto first = pool.fetch(u64{0});
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(pool.unpin(*first, b8{false}).has_value());

    auto second = pool.fetch(u64{1});
    ASSERT_TRUE(second.has_value());
    ASSERT_TRUE(pool.unpin(*second, b8{false}).has_value());
}
