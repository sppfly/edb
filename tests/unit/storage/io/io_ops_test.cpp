// tests/unit/storage/io/io_ops_test.cpp
//
// Tests for EdbStorageIOOps interface, EdbIOConfig defaults, EdbIOVec layout,
// and the default virtual implementations (readv/writev loops, mmap/munmap
// returning NotSupported, sync_range delegating to datasync).

#include "storage/io/io_ops.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>

using namespace edb;

// ---------------------------------------------------------------------------
// Static checks on EdbIOConfig defaults
// ---------------------------------------------------------------------------

TEST(EdbIOConfig, DefaultPageSize) {
    constexpr EdbIOConfig cfg{};
    EXPECT_EQ(cfg.page_size.value, usize{4096}.value);
}

TEST(EdbIOConfig, DefaultUseDirectIO) {
    constexpr EdbIOConfig cfg{};
    EXPECT_EQ(cfg.use_direct_io.value, false);
}

TEST(EdbIOConfig, DefaultQueueDepth) {
    constexpr EdbIOConfig cfg{};
    EXPECT_EQ(cfg.queue_depth.value, u32{1}.value);
}

TEST(EdbIOConfig, CustomValues) {
    constexpr EdbIOConfig cfg{
        .page_size = usize{8192}, .use_direct_io = b8{true}, .queue_depth = u32{32}};
    EXPECT_EQ(cfg.page_size.value, usize{8192}.value);
    EXPECT_EQ(cfg.use_direct_io.value, true);
    EXPECT_EQ(cfg.queue_depth.value, u32{32}.value);
}

// ---------------------------------------------------------------------------
// EdbIOVec layout
// ---------------------------------------------------------------------------

TEST(EdbIOVec, FieldTypes) {
    static_assert(std::is_same_v<decltype(EdbIOVec::offset), u64>);
    static_assert(std::is_same_v<decltype(EdbIOVec::buf), std::span<std::byte>>);
}

// ---------------------------------------------------------------------------
// MockIOOps — minimal concrete backend for testing defaults
// ---------------------------------------------------------------------------

class MockIOOps : public EdbStorageIOOps {
   public:
    // Storage: flat byte buffer simulating a file.
    std::vector<std::byte> storage_;

    auto open(const char* /*path*/, const EdbIOConfig& /*cfg*/) -> EdbStatus override {
        storage_.resize(65536);
        return {};
    }

    auto close() -> EdbStatus override { return {}; }

    auto read(u64 offset, std::span<std::byte> buf) -> EdbResult<usize> override {
        const auto off = offset.value;
        if (off >= storage_.size()) {
            return usize{0};
        }
        const auto avail = storage_.size() - off;
        const auto n = std::min(avail, buf.size());
        std::copy_n(storage_.data() + off, n, buf.data());
        return usize{n};
    }

    auto write(u64 offset, std::span<const std::byte> buf) -> EdbResult<usize> override {
        const auto off = offset.value;
        if (off + buf.size() > storage_.size()) {
            storage_.resize(off + buf.size());
        }
        std::copy(buf.begin(), buf.end(), storage_.data() + off);
        return usize{buf.size()};
    }

    auto sync() -> EdbStatus override { return {}; }
    auto datasync() -> EdbStatus override { return {}; }
    auto truncate(u64 size) -> EdbStatus override {
        storage_.resize(size.value);
        return {};
    }
    auto file_size() -> EdbResult<u64> override { return u64{storage_.size()}; }
};

// ---------------------------------------------------------------------------
// Default readv — loops over read()
// ---------------------------------------------------------------------------

TEST(EdbStorageIOOpsDefaults, ReadvLoopsOverRead) {
    MockIOOps backend;
    ASSERT_TRUE(backend.open("mock", EdbIOConfig{}).has_value());

    // Write two separate regions via the concrete write().
    std::array<std::byte, 4> src0{std::byte{0xAA}, std::byte{0xBB}, std::byte{0xCC},
                                  std::byte{0xDD}};
    std::array<std::byte, 2> src1{std::byte{0x11}, std::byte{0x22}};
    ASSERT_TRUE(backend.write(u64{0}, src0).has_value());
    ASSERT_TRUE(backend.write(u64{4}, src1).has_value());

    // Read both back via the default readv().
    std::array<std::byte, 4> dst0{};
    std::array<std::byte, 2> dst1{};
    std::array<EdbIOVec, 2> iov{EdbIOVec{u64{0}, dst0}, EdbIOVec{u64{4}, dst1}};

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
    ASSERT_TRUE(backend.open("mock", EdbIOConfig{}).has_value());

    std::array<std::byte, 4> buf0{std::byte{0xDE}, std::byte{0xAD}, std::byte{0xBE},
                                  std::byte{0xEF}};
    std::array<std::byte, 3> buf1{std::byte{0x0A}, std::byte{0x0B}, std::byte{0x0C}};

    std::array<EdbIOVec, 2> wv{EdbIOVec{u64{100}, buf0}, EdbIOVec{u64{200}, buf1}};
    // writev takes span<const EdbIOVec>
    auto res = backend.writev(std::span<const EdbIOVec>{wv});
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
    EXPECT_EQ(res.error(), EdbError::NotSupported);
}

TEST(EdbStorageIOOpsDefaults, MunmapReturnsNotSupported) {
    MockIOOps backend;
    auto res = backend.munmap(nullptr, usize{4096});
    ASSERT_FALSE(res.has_value());
    EXPECT_EQ(res.error(), EdbError::NotSupported);
}

// ---------------------------------------------------------------------------
// Default sync_range — delegates to datasync()
// ---------------------------------------------------------------------------

TEST(EdbStorageIOOpsDefaults, SyncRangeDelegatesToDatasync) {
    MockIOOps backend;
    ASSERT_TRUE(backend.open("mock", EdbIOConfig{}).has_value());
    // datasync() on MockIOOps returns success, so sync_range should too.
    auto res = backend.sync_range(u64{0}, usize{4096});
    ASSERT_TRUE(res.has_value());
}
