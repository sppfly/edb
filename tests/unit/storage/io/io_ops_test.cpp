// tests/unit/storage/io/io_ops_test.cpp
//
// Tests for EdbStorageIOOps interface, EdbIOConfig defaults, EdbIOVec layout,
// and the default virtual implementations (readv/writev loops, mmap/munmap
// returning NotSupported, sync_range delegating to datasync).

#include "storage/io/io_ops.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <vector>

using namespace edb;

// ---------------------------------------------------------------------------
// Static checks on EdbIOConfig defaults
// ---------------------------------------------------------------------------

TEST(EdbIOConfig, DefaultPageSize) {
    constexpr IOConfig cfg{};
    EXPECT_EQ(cfg.page_size.value, usize{4096}.value);
}

TEST(EdbIOConfig, DefaultUseDirectIO) {
    constexpr IOConfig cfg{};
    EXPECT_EQ(cfg.use_direct_io.value, false);
}

TEST(EdbIOConfig, DefaultQueueDepth) {
    constexpr IOConfig cfg{};
    EXPECT_EQ(cfg.queue_depth.value, u32{1}.value);
}

TEST(EdbIOConfig, CustomValues) {
    constexpr IOConfig cfg{
        .page_size = usize{8192}, .use_direct_io = b8{true}, .queue_depth = u32{32}};
    EXPECT_EQ(cfg.page_size.value, usize{8192}.value);
    EXPECT_EQ(cfg.use_direct_io.value, true);
    EXPECT_EQ(cfg.queue_depth.value, u32{32}.value);
}

// ---------------------------------------------------------------------------
// EdbIOVec layout
// ---------------------------------------------------------------------------

TEST(EdbIOVec, FieldTypes) {
    static_assert(std::is_same_v<decltype(IOVec::offset), u64>);
    static_assert(std::is_same_v<decltype(IOVec::buf), std::span<std::byte>>);
}

// ---------------------------------------------------------------------------
// MockIOOps — minimal concrete backend for testing defaults
// ---------------------------------------------------------------------------

class MockIOOps : public StorageIOOps {
public:
    // Storage: flat byte buffer simulating a file.
    std::vector<std::byte> storage;

private:
    auto open_impl(const char* /*path*/, const IOConfig& /*cfg*/) -> VoidResult override {
        storage.resize(65536);
        return {};
    }

    auto close_impl() -> VoidResult override { return {}; }

    auto read_impl(u64 offset, std::span<std::byte> buf) -> Result<usize> override {
        const auto off = offset.value;
        if (off >= storage.size()) {
            return usize{0};
        }
        const auto avail = storage.size() - off;
        const auto n = std::min(avail, buf.size());
        auto src = std::span<const std::byte>{storage}.subspan(off, n);
        std::ranges::copy(src, buf.begin());
        return usize{n};
    }

    auto write_impl(u64 offset, std::span<const std::byte> buf) -> Result<usize> override {
        const auto off = offset.value;
        if (off + buf.size() > storage.size()) {
            storage.resize(off + buf.size());
        }
        auto dst = std::span<std::byte>{storage}.subspan(off, buf.size());
        std::ranges::copy(buf, dst.begin());
        return usize{buf.size()};
    }

    auto sync_impl() -> VoidResult override { return {}; }
    auto datasync_impl() -> VoidResult override { return {}; }
    auto truncate_impl(u64 size) -> VoidResult override {
        storage.resize(size.value);
        return {};
    }
    auto file_size_impl() -> Result<u64> override { return u64{storage.size()}; }
};

// ---------------------------------------------------------------------------
// Default readv — loops over read()
// ---------------------------------------------------------------------------

TEST(EdbStorageIOOpsDefaults, ReadvLoopsOverRead) {
    MockIOOps backend;
    ASSERT_TRUE(backend.open("mock", IOConfig{}).has_value());

    // Write two separate regions via the concrete write().
    std::array<std::byte, 4> src0{std::byte{0xAA}, std::byte{0xBB}, std::byte{0xCC},
                                  std::byte{0xDD}};
    std::array<std::byte, 2> src1{std::byte{0x11}, std::byte{0x22}};
    ASSERT_TRUE(backend.write(u64{0}, src0).has_value());
    ASSERT_TRUE(backend.write(u64{4}, src1).has_value());

    // Read both back via the default readv().
    std::array<std::byte, 4> dst0{};
    std::array<std::byte, 2> dst1{};
    std::array<IOVec, 2> iov{IOVec{.offset = u64{0}, .buf = dst0},
                             IOVec{.offset = u64{4}, .buf = dst1}};

    auto res = backend.readv(iov);
    ASSERT_TRUE(res.has_value());
    EXPECT_EQ(res->value, usize{6}.value);
    EXPECT_EQ(dst0, src0);
    EXPECT_EQ(dst1, src1);
}

// ---------------------------------------------------------------------------
// Default writev — loops over write()
// ---------------------------------------------------------------------------

TEST(EdbStorageIOOpsDefaults, WritevLoopsOverWrite) {
    MockIOOps backend;
    ASSERT_TRUE(backend.open("mock", IOConfig{}).has_value());

    std::array<std::byte, 4> buf0{std::byte{0xDE}, std::byte{0xAD}, std::byte{0xBE},
                                  std::byte{0xEF}};
    std::array<std::byte, 3> buf1{std::byte{0x0A}, std::byte{0x0B}, std::byte{0x0C}};

    std::array<IOVec, 2> wv{IOVec{.offset = u64{100}, .buf = buf0},
                            IOVec{.offset = u64{200}, .buf = buf1}};
    // writev takes span<const EdbIOVec>
    auto res = backend.writev(std::span<const IOVec>{wv});
    ASSERT_TRUE(res.has_value());
    EXPECT_EQ(res->value, usize{7}.value);

    // Verify regions were actually written.
    std::array<std::byte, 4> rdst0{};
    std::array<std::byte, 3> rdst1{};
    ASSERT_TRUE(backend.read(u64{100}, rdst0).has_value());
    ASSERT_TRUE(backend.read(u64{200}, rdst1).has_value());
    EXPECT_EQ(rdst0, buf0);
    EXPECT_EQ(rdst1, buf1);
}

// ---------------------------------------------------------------------------
// Default mmap / munmap — return NotSupported
// ---------------------------------------------------------------------------

TEST(EdbStorageIOOpsDefaults, MmapReturnsNotSupported) {
    MockIOOps backend;
    auto res = backend.mmap(u64{0}, usize{4096}, i32{1});
    ASSERT_FALSE(res.has_value());
    EXPECT_EQ(res.error(), Error::NotSupported);
}

TEST(EdbStorageIOOpsDefaults, MunmapReturnsNotSupported) {
    MockIOOps backend;
    auto res = backend.munmap(nullptr, usize{4096});
    ASSERT_FALSE(res.has_value());
    EXPECT_EQ(res.error(), Error::NotSupported);
}

// ---------------------------------------------------------------------------
// Default sync_range — delegates to datasync()
// ---------------------------------------------------------------------------

TEST(EdbStorageIOOpsDefaults, SyncRangeDelegatesToDatasync) {
    MockIOOps backend;
    ASSERT_TRUE(backend.open("mock", IOConfig{}).has_value());
    // datasync() on MockIOOps returns success, so sync_range should too.
    auto res = backend.sync_range(u64{0}, usize{4096});
    ASSERT_TRUE(res.has_value());
}
