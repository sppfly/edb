// tests/unit/storage/io/posix_io_test.cpp
//
// Unit tests for PosixIO — the POSIX file I/O backend.
//
// Each test opens a fresh temporary file in SetUp() and deletes it in
// TearDown() so tests are hermetic and leave no filesystem debris.

#include "storage/io/posix/posix_io.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>

using namespace edb;

// ---------------------------------------------------------------------------
// Test fixture
// ---------------------------------------------------------------------------

class PosixIOTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create a unique temp file that survives for the test body.
        // mkstemp requires a mutable template.
        std::array<char, 25> tmpl{"/tmp/edb_posix_io_XXXXXX"};
        // raw-primitive: mkstemp returns int fd; we close it immediately and
        // use the path via our PosixIO::open(). We keep the fd in
        // tmp_fd so we can unlink the file in TearDown.
        tmp_fd = ::mkstemp(tmpl.data());  // raw-primitive: mkstemp returns int
        ASSERT_GE(tmp_fd, 0) << "mkstemp failed: " << strerror(errno);
        ASSERT_EQ(::close(tmp_fd), 0);  // raw-primitive: close takes int
        tmp_fd = -1;
        tmp_path = tmpl.data();
    }

    void TearDown() override {
        EXPECT_TRUE(io.close().has_value());
        if (!tmp_path.empty()) {
            std::filesystem::remove(tmp_path);
        }
    }

    auto backend() -> PosixIO& { return io; }
    [[nodiscard]] auto temp_path() const -> const std::string& { return tmp_path; }

private:
    PosixIO io;
    std::string tmp_path;
    int tmp_fd{-1};  // raw-primitive: unused after SetUp; kept for type documentation
};

// ---------------------------------------------------------------------------
// open / close
// ---------------------------------------------------------------------------

TEST_F(PosixIOTest, OpenValidPath) {
    auto res = backend().open(temp_path().c_str(), IOConfig{});
    EXPECT_TRUE(res.has_value());
}

TEST_F(PosixIOTest, OpenUnwritablePath) {
    auto res = backend().open("/proc/no_such_file_edb", IOConfig{});
    EXPECT_FALSE(res.has_value());
    EXPECT_EQ(res.error(), Error::IoError);
}

TEST_F(PosixIOTest, CloseAfterOpen) {
    ASSERT_TRUE(backend().open(temp_path().c_str(), IOConfig{}).has_value());
    auto res = backend().close();
    EXPECT_TRUE(res.has_value());
}

TEST_F(PosixIOTest, CloseIdempotent) {
    // close() on an already-closed backend is a no-op.
    EXPECT_TRUE(backend().close().has_value());
}

// ---------------------------------------------------------------------------
// read / write before open — must return IoError
// ---------------------------------------------------------------------------

TEST_F(PosixIOTest, ReadBeforeOpen) {
    std::array<std::byte, 4> buf{};
    auto res = backend().read(u64{0}, buf);
    ASSERT_FALSE(res.has_value());
    EXPECT_EQ(res.error(), Error::IoError);
}

TEST_F(PosixIOTest, WriteBeforeOpen) {
    const std::array<std::byte, 4> buf{};
    auto res = backend().write(u64{0}, buf);
    ASSERT_FALSE(res.has_value());
    EXPECT_EQ(res.error(), Error::IoError);
}

// ---------------------------------------------------------------------------
// write + read roundtrip
// ---------------------------------------------------------------------------

TEST_F(PosixIOTest, WriteReadRoundtrip4096) {
    ASSERT_TRUE(backend().open(temp_path().c_str(), IOConfig{}).has_value());

    // Fill a 4096-byte page with a pattern.
    std::array<std::byte, 4096> src{};
    auto pattern = std::size_t{0};
    for (auto& value : src) {
        value = std::byte{
            static_cast<unsigned char>(pattern & 0xFFU)};  // raw-primitive: cast for pattern fill
        ++pattern;
    }

    auto wres = backend().write(u64{0}, src);
    ASSERT_TRUE(wres.has_value());
    EXPECT_EQ(wres->value, usize{4096}.value);

    std::array<std::byte, 4096> dst{};
    auto rres = backend().read(u64{0}, dst);
    ASSERT_TRUE(rres.has_value());
    EXPECT_EQ(rres->value, usize{4096}.value);
    EXPECT_EQ(src, dst);
}

TEST_F(PosixIOTest, WriteReadOneByte) {
    ASSERT_TRUE(backend().open(temp_path().c_str(), IOConfig{}).has_value());

    std::array<std::byte, 1> src{std::byte{0xAB}};
    ASSERT_TRUE(backend().write(u64{0}, src).has_value());

    std::array<std::byte, 1> dst{};
    auto res = backend().read(u64{0}, dst);
    ASSERT_TRUE(res.has_value());
    EXPECT_EQ(dst[0], std::byte{0xAB});
}

TEST_F(PosixIOTest, WriteReadAtNonZeroOffset) {
    ASSERT_TRUE(backend().open(temp_path().c_str(), IOConfig{}).has_value());

    // Write at offset 8192.
    std::array<std::byte, 16> src{};
    auto pattern = std::size_t{0};
    for (auto& value : src) {
        value =
            std::byte{static_cast<unsigned char>(pattern)};  // raw-primitive: cast for pattern fill
        ++pattern;
    }
    ASSERT_TRUE(backend().write(u64{8192}, src).has_value());

    std::array<std::byte, 16> dst{};
    ASSERT_TRUE(backend().read(u64{8192}, dst).has_value());
    EXPECT_EQ(src, dst);
}

TEST_F(PosixIOTest, ReadPastEOFReturnsZeroBytes) {
    ASSERT_TRUE(backend().open(temp_path().c_str(), IOConfig{}).has_value());
    // File is empty (0 bytes). Reading from offset 0 should return 0.
    std::array<std::byte, 8> buf{};
    auto res = backend().read(u64{0}, buf);
    ASSERT_TRUE(res.has_value());
    EXPECT_EQ(res->value, usize{0}.value);
}

// ---------------------------------------------------------------------------
// file_size
// ---------------------------------------------------------------------------

TEST_F(PosixIOTest, FileSizeAfterWrite) {
    ASSERT_TRUE(backend().open(temp_path().c_str(), IOConfig{}).has_value());

    std::array<std::byte, 512> buf{};
    ASSERT_TRUE(backend().write(u64{0}, buf).has_value());

    auto res = backend().file_size();
    ASSERT_TRUE(res.has_value());
    EXPECT_EQ(res->value, u64{512}.value);
}

TEST_F(PosixIOTest, FileSizeBeforeOpen) {
    auto res = backend().file_size();
    ASSERT_FALSE(res.has_value());
    EXPECT_EQ(res.error(), Error::IoError);
}

// ---------------------------------------------------------------------------
// truncate
// ---------------------------------------------------------------------------

TEST_F(PosixIOTest, TruncateShrinksFile) {
    ASSERT_TRUE(backend().open(temp_path().c_str(), IOConfig{}).has_value());

    std::array<std::byte, 1024> buf{};
    ASSERT_TRUE(backend().write(u64{0}, buf).has_value());

    ASSERT_TRUE(backend().truncate(u64{512}).has_value());

    auto sz = backend().file_size();
    ASSERT_TRUE(sz.has_value());
    EXPECT_EQ(sz->value, u64{512}.value);
}

TEST_F(PosixIOTest, TruncateExtendsFile) {
    ASSERT_TRUE(backend().open(temp_path().c_str(), IOConfig{}).has_value());
    ASSERT_TRUE(backend().truncate(u64{8192}).has_value());

    auto sz = backend().file_size();
    ASSERT_TRUE(sz.has_value());
    EXPECT_EQ(sz->value, u64{8192}.value);
}

// ---------------------------------------------------------------------------
// sync / datasync
// ---------------------------------------------------------------------------

TEST_F(PosixIOTest, SyncSucceeds) {
    ASSERT_TRUE(backend().open(temp_path().c_str(), IOConfig{}).has_value());
    EXPECT_TRUE(backend().sync().has_value());
}

TEST_F(PosixIOTest, DatasyncSucceeds) {
    ASSERT_TRUE(backend().open(temp_path().c_str(), IOConfig{}).has_value());
    EXPECT_TRUE(backend().datasync().has_value());
}

TEST_F(PosixIOTest, SyncBeforeOpenReturnsError) {
    EXPECT_FALSE(backend().sync().has_value());
}

TEST_F(PosixIOTest, DatasyncBeforeOpenReturnsError) {
    EXPECT_FALSE(backend().datasync().has_value());
}

// ---------------------------------------------------------------------------
// sync_range
// ---------------------------------------------------------------------------

TEST_F(PosixIOTest, SyncRangeOnWrittenRegion) {
    ASSERT_TRUE(backend().open(temp_path().c_str(), IOConfig{}).has_value());

    std::array<std::byte, 4096> buf{};
    ASSERT_TRUE(backend().write(u64{0}, buf).has_value());

    EXPECT_TRUE(backend().sync_range(u64{0}, usize{4096}).has_value());
}
