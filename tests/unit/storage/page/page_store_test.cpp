// tests/unit/storage/page/page_store_test.cpp
//
// Unit tests for EdbPageStore. The mock backend is byte-addressed so the tests
// verify that PageStore performs the page_id -> offset mapping itself.

#include "storage/page/page_store.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <vector>

using namespace edb;

class MockIOOps : public StorageIO {
public:
    std::vector<std::byte> storage;
    usize sync_calls{0};

    auto resize_to(usize size) -> void { storage.resize(size.value); }

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
            return std::unexpected(Error::IoError);
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

namespace {

auto patterned_page() -> std::array<std::byte, 4096> {
    std::array<std::byte, 4096> page{};
    auto pattern = std::size_t{0};
    for (auto& value : page) {
        value = std::byte{static_cast<unsigned char>((pattern + 17U) & 0xFFU)};
        ++pattern;
    }
    return page;
}

auto is_zero_page(const std::array<std::byte, 4096>& page) -> bool {
    return std::ranges::all_of(page, [](std::byte value) { return value == std::byte{0}; });
}

}  // namespace

TEST(EdbPageStoreConfig, DefaultPageSize) {
    constexpr PageStoreConfig cfg{};
    EXPECT_EQ(cfg.page_size.value, usize{8192}.value);
}

TEST(EdbPageStore, OpenSetsPageSize) {
    MockIOOps io;
    PageStore store;

    ASSERT_TRUE(store.open(io, PageStoreConfig{.page_size = usize{4096}}).has_value());
    EXPECT_EQ(store.page_size().value, usize{4096}.value);
}

TEST(EdbPageStore, PageCountStartsAtZero) {
    MockIOOps io;
    PageStore store;

    ASSERT_TRUE(store.open(io, PageStoreConfig{.page_size = usize{4096}}).has_value());

    auto count = store.page_count();
    ASSERT_TRUE(count.has_value());
    EXPECT_EQ(count->value, u64{0}.value);
}

TEST(EdbPageStore, AllocatePageExtendsFile) {
    MockIOOps io;
    PageStore store;

    ASSERT_TRUE(store.open(io, PageStoreConfig{.page_size = usize{4096}}).has_value());

    auto first = store.allocate_page();
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(first->value, u64{0}.value);
    EXPECT_EQ(io.storage.size(), usize{4096}.value);

    auto second = store.allocate_page();
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(second->value, u64{1}.value);
    EXPECT_EQ(io.storage.size(), usize{8192}.value);
}

TEST(EdbPageStore, WriteReadPageRoundTrip) {
    MockIOOps io;
    PageStore store;

    ASSERT_TRUE(store.open(io, PageStoreConfig{.page_size = usize{4096}}).has_value());
    ASSERT_TRUE(store.allocate_page().has_value());
    ASSERT_TRUE(store.allocate_page().has_value());

    const auto src = patterned_page();
    ASSERT_TRUE(store.write_page(u64{1}, src).has_value());

    std::array<std::byte, 4096> dst{};
    ASSERT_TRUE(store.read_page(u64{1}, dst).has_value());
    EXPECT_EQ(dst, src);
}

TEST(EdbPageStore, AllocatedPageReadsAsZeroFilled) {
    MockIOOps io;
    PageStore store;

    ASSERT_TRUE(store.open(io, PageStoreConfig{.page_size = usize{4096}}).has_value());
    ASSERT_TRUE(store.allocate_page().has_value());

    std::array<std::byte, 4096> page_zero{};
    ASSERT_TRUE(store.read_page(u64{0}, page_zero).has_value());
    EXPECT_TRUE(is_zero_page(page_zero));
}

TEST(EdbPageStore, WriteUnallocatedPageFails) {
    MockIOOps io;
    PageStore store;

    ASSERT_TRUE(store.open(io, PageStoreConfig{.page_size = usize{4096}}).has_value());

    const std::array<std::byte, 4096> src{};
    auto result = store.write_page(u64{0}, src);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), Error::PageNotFound);
}

TEST(EdbPageStore, ReadUnallocatedPageFails) {
    MockIOOps io;
    PageStore store;

    ASSERT_TRUE(store.open(io, PageStoreConfig{.page_size = usize{4096}}).has_value());

    std::array<std::byte, 4096> dst{};
    auto result = store.read_page(u64{0}, dst);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), Error::PageNotFound);
}

TEST(EdbPageStore, MisalignedFileSizeIsCorruption) {
    MockIOOps io;
    PageStore store;
    io.resize_to(usize{4097});

    ASSERT_TRUE(store.open(io, PageStoreConfig{.page_size = usize{4096}}).has_value());

    auto count = store.page_count();
    ASSERT_FALSE(count.has_value());
    EXPECT_EQ(count.error(), Error::Corruption);
}

TEST(EdbPageStore, SyncDelegatesToBackend) {
    MockIOOps io;
    PageStore store;

    ASSERT_TRUE(store.open(io, PageStoreConfig{.page_size = usize{4096}}).has_value());
    ASSERT_TRUE(store.sync().has_value());
    EXPECT_EQ(io.sync_calls.value, usize{1}.value);
}

TEST(EdbPageStore, OperationsBeforeOpenFail) {
    PageStore store;
    std::array<std::byte, 8192> page{};

    EXPECT_EQ(store.page_count().error(), Error::IoError);
    EXPECT_EQ(store.allocate_page().error(), Error::IoError);
    EXPECT_EQ(store.read_page(u64{0}, page).error(), Error::IoError);
    EXPECT_EQ(store.write_page(u64{0}, page).error(), Error::IoError);
    EXPECT_EQ(store.sync().error(), Error::IoError);
}
