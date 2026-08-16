// tests/storage/io/io_bench.cpp
//
// I/O throughput benchmark for EDB storage backends.
//
// Usage:
//   io_bench [page_size_bytes [page_count]]
//   io_bench 4096 4096     # 4K pages x 4096 = 16 MB
//   io_bench 8192 1024     # 8K pages x 1024 = 8 MB
//
// Reports sequential write, sequential read, and random read throughput
// in MB/s to stdout. The benchmark writes N pages of page_size bytes
// sequentially, then reads them back sequentially, then reads them back
// in a pseudo-random order.
//
// This is a standalone executable — not registered with CTest. Build with
//   cmake -DEDB_BUILD_BENCH=ON ..
//   make -j io_bench

#include <unistd.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <iostream>
#include <numeric>
#include <print>
#include <span>
#include <vector>

#include "storage/io/posix/posix_io.hpp"

using namespace edb;

namespace {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

double bytes_to_mib(std::size_t bytes) {
    // raw-primitive: division for MB/s display
    return static_cast<double>(bytes) / (1024.0 * 1024.0);
}

double elapsed_s(std::chrono::steady_clock::time_point start,
                 std::chrono::steady_clock::time_point end) {
    return std::chrono::duration<double>(end - start).count();
}

// Simple LCG for pseudo-random page order (reproducible, no stdlib overhead).
class Lcg {
public:
    explicit Lcg(std::uint64_t seed) : state(seed) {}
    std::uint64_t next() {
        state = (state * 6364136223846793005ULL) + 1442695040888963407ULL;
        return state;
    }

private:
    std::uint64_t state;
};

// ---------------------------------------------------------------------------
// Benchmark runners
// ---------------------------------------------------------------------------

void run_sequential_write(PosixIO& io, const std::vector<std::byte>& page, std::size_t page_count) {
    const std::size_t psz = page.size();

    auto start = std::chrono::steady_clock::now();
    for (std::size_t i = 0; i < page_count; ++i) {
        // raw-primitive: multiplication for offset
        const auto off = static_cast<std::uint64_t>(i * psz);
        auto res = io.write(u64{off}, std::span<const std::byte>{page});
        if (!res) {
            std::println(stderr, "write error at page {}", i);
            return;
        }
    }
    auto end = std::chrono::steady_clock::now();

    const double secs = elapsed_s(start, end);
    const double total_mib = bytes_to_mib(page_count * psz);
    std::println("sequential write: {:.1f} MiB in {:.3f} s = {:.1f} MiB/s", total_mib, secs,
                 total_mib / secs);
}

void run_sequential_read(PosixIO& io, std::size_t page_size, std::size_t page_count) {
    std::vector<std::byte> buf(page_size);

    auto start = std::chrono::steady_clock::now();
    for (std::size_t i = 0; i < page_count; ++i) {
        const auto off = static_cast<std::uint64_t>(i * page_size);  // raw-primitive
        auto res = io.read(u64{off}, std::span<std::byte>{buf});
        if (!res) {
            std::println(stderr, "read error at page {}", i);
            return;
        }
    }
    auto end = std::chrono::steady_clock::now();

    const double secs = elapsed_s(start, end);
    const double total_mib = bytes_to_mib(page_count * page_size);
    std::println("sequential read:  {:.1f} MiB in {:.3f} s = {:.1f} MiB/s", total_mib, secs,
                 total_mib / secs);
}

void run_random_read(PosixIO& io, std::size_t page_size, std::size_t page_count) {
    // Build a shuffled page index.
    std::vector<std::size_t> order(page_count);
    std::ranges::iota(order, std::size_t{0});
    Lcg lcg{0xDEADBEEF'CAFEBABE};
    for (std::size_t i = page_count - 1; i > 0; --i) {
        const auto j = static_cast<std::size_t>(lcg.next() % (i + 1));  // raw-primitive
        std::swap(order[i], order[j]);
    }

    std::vector<std::byte> buf(page_size);
    auto start = std::chrono::steady_clock::now();
    for (std::size_t idx : order) {
        const auto off = static_cast<std::uint64_t>(idx * page_size);  // raw-primitive
        auto res = io.read(u64{off}, std::span<std::byte>{buf});
        if (!res) {
            std::println(stderr, "read error at page {}", idx);
            return;
        }
    }
    auto end = std::chrono::steady_clock::now();

    const double secs = elapsed_s(start, end);
    const double total_mib = bytes_to_mib(page_count * page_size);
    std::println("random read:      {:.1f} MiB in {:.3f} s = {:.1f} MiB/s", total_mib, secs,
                 total_mib / secs);
}

}  // namespace

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main(int argc, char* argv[]) try {
    // Defaults: 4 KiB pages, 4096 pages = 16 MiB total.
    // raw-primitive: atol / argc are POSIX / C main signature (no wrappers there)
    const std::span<char*> args{argv, static_cast<std::size_t>(argc)};
    std::size_t page_size = (args.size() > 1) ? static_cast<std::size_t>(std::atol(args[1])) : 4096;
    std::size_t page_count =
        (args.size() > 2) ? static_cast<std::size_t>(std::atol(args[2])) : 4096;

    if (page_size == 0 || page_count == 0) {
        std::println(stderr, "Usage: io_bench [page_size_bytes [page_count]]");
        return 1;
    }

    // Temp file for the benchmark.
    std::array<char, 25> tmpl{"/tmp/edb_io_bench_XXXXXX"};
    // raw-primitive: mkstemp returns int
    const int tmp_fd = ::mkstemp(tmpl.data());
    if (tmp_fd < 0) {
        std::println(stderr, "mkstemp failed");
        return 1;
    }
    ::close(tmp_fd);  // raw-primitive: close takes int
    const std::string bench_path = tmpl.data();

    std::println("io_bench: page_size={} bytes, page_count={}, total={:.1f} MiB", page_size,
                 page_count, bytes_to_mib(page_size * page_count));
    std::println("file: {}", bench_path);

    PosixIO io;
    EdbIOConfig cfg{.page_size = usize{page_size}};
    if (!io.open(bench_path.c_str(), cfg)) {
        std::println(stderr, "Failed to open bench file");
        std::filesystem::remove(bench_path);
        return 1;
    }

    // Fill a page buffer with a pattern.
    std::vector<std::byte> page(page_size);
    for (std::size_t i = 0; i < page_size; ++i) {
        page[i] = std::byte{static_cast<unsigned char>(i & 0xFFU)};  // raw-primitive: pattern fill
    }

    run_sequential_write(io, page, page_count);
    if (auto sync_res = io.sync(); !sync_res) {
        std::println(stderr, "sync failed");
        std::filesystem::remove(bench_path);
        return 1;
    }
    run_sequential_read(io, page_size, page_count);
    run_random_read(io, page_size, page_count);

    if (auto close_res = io.close(); !close_res) {
        std::println(stderr, "close failed");
        std::filesystem::remove(bench_path);
        return 1;
    }
    std::filesystem::remove(bench_path);
    return 0;
} catch (const std::exception& ex) {
    std::fputs("io_bench failed: ", stderr);
    std::fputs(ex.what(), stderr);
    std::fputc('\n', stderr);
    return 1;
} catch (...) {
    std::fputs("io_bench failed with an unknown exception\n", stderr);
    return 1;
}
