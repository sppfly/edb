// src/storage/io/posix/posix_io.cpp
//
// POSIX file I/O backend implementation.

#include "storage/io/posix/posix_io.hpp"

#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

// open(2) flags
#include <fcntl.h>

namespace edb {

// ---------------------------------------------------------------------------
// Destructor — ensure the file is closed on destruction.
// ---------------------------------------------------------------------------

PosixIO::~PosixIO() {
    if (fd >= 0) {
        // Best-effort: ignore errors in destructor.
        ::close(fd);  // raw-primitive: POSIX close takes int fd
        fd = -1;
    }
}

// ---------------------------------------------------------------------------
// Private helper
// ---------------------------------------------------------------------------

auto PosixIO::check_open() const -> EdbStatus {
    if (fd < 0) {
        return std::unexpected(EdbError::IoError);
    }
    return {};
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

auto PosixIO::open_impl(const char* path, const EdbIOConfig& cfg) -> EdbStatus {
    if (fd >= 0) {
        // Already open — close first.
        if (auto res = close_impl(); !res) {
            return res;
        }
    }

    // raw-primitive: open(2) flags and mode are POSIX int constants
    int flags = O_RDWR | O_CREAT;
    if (cfg.use_direct_io.value) {
#ifdef O_DIRECT
        flags |= O_DIRECT;
#endif
    }

    // raw-primitive: open(2) returns int; mode 0600 is an octal int literal
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg): POSIX open(2) is a C vararg API.
    const int new_fd = ::open(path, flags, 0600);
    if (new_fd < 0) {
        return std::unexpected(EdbError::IoError);
    }

    fd = new_fd;
    path_name = path;
    return {};
}

auto PosixIO::close_impl() -> EdbStatus {
    if (fd < 0) {
        return {};  // Already closed — no-op.
    }
    // raw-primitive: close(2) takes int fd
    if (::close(fd) != 0) {
        return std::unexpected(EdbError::IoError);
    }
    fd = -1;
    path_name.clear();
    return {};
}

// ---------------------------------------------------------------------------
// Synchronous I/O
// ---------------------------------------------------------------------------

auto PosixIO::read_impl(u64 offset, std::span<std::byte> buf) -> EdbResult<usize> {
    if (auto s = check_open(); !s) {
        return std::unexpected(s.error());
    }

    std::size_t bytes_read = 0;  // raw-primitive: pread64 returns ssize_t / takes size_t
    auto off = static_cast<std::int64_t>(offset.value);  // raw-primitive: off64_t is int64_t

    while (bytes_read < buf.size()) {
        auto chunk = buf.subspan(bytes_read);
        // raw-primitive: pread64 signature uses ssize_t / size_t / off_t
        const ssize_t n = ::pread64(fd, chunk.data(), chunk.size(), static_cast<off64_t>(off));
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return std::unexpected(EdbError::IoError);
        }
        if (n == 0) {
            // EOF — return however many bytes we read so far.
            break;
        }
        bytes_read += static_cast<std::size_t>(n);
        off += n;
    }

    return usize{bytes_read};
}

auto PosixIO::write_impl(u64 offset, std::span<const std::byte> buf) -> EdbResult<usize> {
    if (auto s = check_open(); !s) {
        return std::unexpected(s.error());
    }

    std::size_t bytes_written = 0;                       // raw-primitive: pwrite64 takes size_t
    auto off = static_cast<std::int64_t>(offset.value);  // raw-primitive: off64_t

    while (bytes_written < buf.size()) {
        auto chunk = buf.subspan(bytes_written);
        // raw-primitive: pwrite64 returns ssize_t
        const ssize_t n = ::pwrite64(fd, chunk.data(), chunk.size(), static_cast<off64_t>(off));
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return std::unexpected(EdbError::IoError);
        }
        bytes_written += static_cast<std::size_t>(n);
        off += n;
    }

    return usize{buf.size()};
}

// ---------------------------------------------------------------------------
// Memory mapping
// ---------------------------------------------------------------------------

auto PosixIO::mmap_impl(u64 offset, usize len, i32 prot) -> EdbResult<std::byte*> {
    if (auto s = check_open(); !s) {
        return std::unexpected(s.error());
    }

    // raw-primitive: mmap(2) takes void*, size_t, int, int, int, off_t
    void* addr = ::mmap(nullptr, static_cast<std::size_t>(len.value), static_cast<int>(prot.value),
                        MAP_SHARED, fd, static_cast<off_t>(offset.value));

    if (addr == MAP_FAILED) {  // NOLINT(cppcoreguidelines-pro-type-cstyle-cast)
        return std::unexpected(EdbError::IoError);
    }
    return static_cast<std::byte*>(addr);
}

auto PosixIO::munmap_impl(std::byte* addr, usize len) -> EdbStatus {
    // raw-primitive: munmap takes void*, size_t
    if (::munmap(addr, static_cast<std::size_t>(len.value)) != 0) {
        return std::unexpected(EdbError::IoError);
    }
    return {};
}

// ---------------------------------------------------------------------------
// Durability
// ---------------------------------------------------------------------------

auto PosixIO::sync_impl() -> EdbStatus {
    if (auto s = check_open(); !s) {
        return s;
    }
    // raw-primitive: fsync takes int fd
    if (::fsync(fd) != 0) {
        return std::unexpected(EdbError::IoError);
    }
    return {};
}

auto PosixIO::datasync_impl() -> EdbStatus {
    if (auto s = check_open(); !s) {
        return s;
    }
    // raw-primitive: fdatasync takes int fd
    if (::fdatasync(fd) != 0) {
        return std::unexpected(EdbError::IoError);
    }
    return {};
}

auto PosixIO::sync_range_impl(u64 offset, usize len) -> EdbStatus {
    if (auto s = check_open(); !s) {
        return s;
    }
#ifdef __linux__
    // raw-primitive: sync_file_range takes int, off64_t, off64_t, unsigned int
    if (::sync_file_range(fd, static_cast<off64_t>(offset.value), static_cast<off64_t>(len.value),
                          SYNC_FILE_RANGE_WRITE) != 0) {
        return std::unexpected(EdbError::IoError);
    }
    return {};
#else
    (void)offset;
    (void)len;
    return datasync_impl();
#endif
}

// ---------------------------------------------------------------------------
// File management
// ---------------------------------------------------------------------------

auto PosixIO::truncate_impl(u64 size) -> EdbStatus {
    if (auto s = check_open(); !s) {
        return s;
    }
    // raw-primitive: ftruncate64 takes int fd, off64_t
    if (::ftruncate64(fd, static_cast<off64_t>(size.value)) != 0) {
        return std::unexpected(EdbError::IoError);
    }
    return {};
}

auto PosixIO::file_size_impl() -> EdbResult<u64> {
    if (auto s = check_open(); !s) {
        return std::unexpected(s.error());
    }
    // raw-primitive: lseek64 takes int, off64_t, int; returns off64_t
    const off64_t sz = ::lseek64(fd, 0, SEEK_END);
    if (sz < 0) {
        return std::unexpected(EdbError::IoError);
    }
    return u64{static_cast<std::uint64_t>(sz)};
}

}  // namespace edb
