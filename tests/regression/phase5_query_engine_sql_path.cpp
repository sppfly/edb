// tests/regression/phase5_query_engine_sql_path.cpp

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "catalog/catalog.hpp"
#include "query/query_engine.hpp"
#include "storage/io/io_ops.hpp"
#include "types/builtin_types.hpp"

#ifndef EDB_REGRESSION_SOURCE_DIR
#define EDB_REGRESSION_SOURCE_DIR "tests/regression"
#endif

using namespace edb;

namespace {

class SharedMemoryIO final : public StorageIOOps {
   public:
    explicit SharedMemoryIO(std::shared_ptr<std::vector<std::byte>> bytes)
        : storage{std::move(bytes)} {}

   private:
    auto open_impl(const char* /*path*/, const IOConfig& /*cfg*/) -> VoidResult override {
        return {};
    }

    auto close_impl() -> VoidResult override { return {}; }

    auto read_impl(u64 offset, std::span<std::byte> buf) -> Result<usize> override {
        const auto off = offset.value;
        if (off >= storage->size()) {
            return usize{0};
        }
        const auto available = storage->size() - off;
        const auto count = std::min(available, buf.size());
        auto src = std::span<const std::byte>{*storage}.subspan(off, count);
        std::ranges::copy(src, buf.begin());
        return usize{count};
    }

    auto write_impl(u64 offset, std::span<const std::byte> buf) -> Result<usize> override {
        const auto off = offset.value;
        if ((off + buf.size()) > storage->size()) {
            storage->resize(off + buf.size());
        }
        auto dst = std::span<std::byte>{*storage}.subspan(off, buf.size());
        std::ranges::copy(buf, dst.begin());
        return usize{buf.size()};
    }

    auto sync_impl() -> VoidResult override { return {}; }
    auto datasync_impl() -> VoidResult override { return {}; }
    auto truncate_impl(u64 size) -> VoidResult override {
        storage->resize(size.value);
        return {};
    }
    auto file_size_impl() -> Result<u64> override { return u64{storage->size()}; }

    std::shared_ptr<std::vector<std::byte>> storage;
};

class MemoryRelationBackendFactory final : public RelationBackendFactory {
   public:
    auto open_backend(u32 relation_oid, std::string_view /*relation_name*/)
        -> Result<std::unique_ptr<StorageIOOps>> override {
        auto& bytes = relations[relation_oid];
        if (bytes == nullptr) {
            bytes = std::make_shared<std::vector<std::byte>>();
        }
        return std::make_unique<SharedMemoryIO>(bytes);
    }

   private:
    std::unordered_map<u32, std::shared_ptr<std::vector<std::byte>>> relations;
};

auto value_text(const TypeRegistry& registry, const Value& value) -> std::string {
    auto type = registry.lookup(value.type_oid);
    EXPECT_TRUE(type.has_value());
    return (*type)->to_text(value.bytes);
}

auto read_text_file(const std::filesystem::path& path) -> std::string {
    std::ifstream input{path};
    EXPECT_TRUE(input.is_open()) << path;
    std::ostringstream text;
    text << input.rdbuf();
    return text.str();
}

auto trim_sql(std::string_view text) -> std::string_view {
    const auto start = text.find_first_not_of(" \t\r\n");
    if (start == std::string_view::npos) {
        return {};
    }

    const auto end = text.find_last_not_of(" \t\r\n");
    return text.substr(start, (end - start) + 1U);
}

auto split_sql_statements(std::string_view sql) -> std::vector<std::string> {
    std::vector<std::string> statements;
    std::string current;
    b8 in_string{false};

    for (const auto ch : sql) {
        if (ch == ';' && !static_cast<bool>(in_string)) {
            const auto statement = trim_sql(current);
            if (!statement.empty()) {
                statements.emplace_back(statement);
            }
            current.clear();
            continue;
        }

        current.push_back(ch);
        if (ch == '\'') {
            in_string = b8{!static_cast<bool>(in_string)};
        }
    }

    const auto statement = trim_sql(current);
    if (!statement.empty()) {
        statements.emplace_back(statement);
    }
    return statements;
}

auto append_query_result(std::ostringstream& output, const QueryResult& result,
                         const TypeRegistry& registry) -> void {
    if (result.rows.empty()) {
        output << "OK\n\n";
        return;
    }

    const auto& first_row = result.rows.front();
    for (std::size_t index = 0; index < first_row.columns.size(); ++index) {
        if (index != std::size_t{0}) {
            output << " | ";
        }
        output << first_row.columns[index].name;
    }
    output << '\n';

    for (const auto& row : result.rows) {
        for (std::size_t index = 0; index < row.values.size(); ++index) {
            if (index != std::size_t{0}) {
                output << " | ";
            }
            output << value_text(registry, row.values[index]);
        }
        output << '\n';
    }
    output << '(' << result.rows.size() << " row";
    if (result.rows.size() != std::size_t{1}) {
        output << 's';
    }
    output << ")\n\n";
}

auto run_regression_script(std::string_view sql, QueryEngine& engine, const TypeRegistry& registry)
    -> std::string {
    std::ostringstream output;
    for (const auto& statement : split_sql_statements(sql)) {
        auto result = engine.execute(statement);
        if (!result) {
            output << "ERROR: " << edb_error_name(result.error()) << ": " << engine.error_message()
                   << "\n\n";
            continue;
        }
        append_query_result(output, *result, registry);
    }
    return output.str();
}

class Phase5QueryEngineRegression : public ::testing::Test {
   protected:
    void SetUp() override {
        ASSERT_TRUE(register_builtin_types(registry).has_value());
        ASSERT_TRUE(catalog.open().has_value());
    }

    [[nodiscard]] static auto default_engine_config() -> EngineConfig {
        return EngineConfig{.page_size = usize{512}, .buffer_pool_pages = usize{4}};
    }

   public:
    TypeRegistry registry;
    MemoryRelationBackendFactory factory;
    Catalog catalog{registry, factory, default_engine_config()};
};

TEST_F(Phase5QueryEngineRegression, BasicSqlFileMatchesExpectedOutput) {
    QueryEngine engine{catalog, registry};

    const auto root = std::filesystem::path{EDB_REGRESSION_SOURCE_DIR};
    const auto sql = read_text_file(root / "sql" / "phase5_basic.sql");
    const auto expected = read_text_file(root / "expected" / "phase5_basic.out");
    EXPECT_EQ(run_regression_script(sql, engine, registry), expected);

    auto table = catalog.open_table("users");
    ASSERT_TRUE(table.has_value());
    auto stored_rows = (*table)->scan();
    ASSERT_TRUE(stored_rows.has_value());
    EXPECT_EQ(stored_rows->size(), std::size_t{2});
}

TEST_F(Phase5QueryEngineRegression, Phase6TransactionsFileMatchesExpectedOutput) {
    QueryEngine engine{catalog, registry};

    const auto root = std::filesystem::path{EDB_REGRESSION_SOURCE_DIR};
    const auto sql = read_text_file(root / "sql" / "phase6_transactions.sql");
    const auto expected = read_text_file(root / "expected" / "phase6_transactions.out");
    EXPECT_EQ(run_regression_script(sql, engine, registry), expected);
}

}  // namespace