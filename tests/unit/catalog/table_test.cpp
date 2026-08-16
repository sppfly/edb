// tests/unit/catalog/table_test.cpp

#include "catalog/table.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <span>
#include <string>
#include <vector>

#include "storage/engine/heap/heap_engine.hpp"
#include "types/builtin_types.hpp"

using namespace edb;

namespace {

class MockCatalogIOOps : public StorageIOOps {
public:
    std::vector<std::byte> storage;

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

    auto sync_impl() -> VoidResult override { return {}; }
    auto datasync_impl() -> VoidResult override { return {}; }
    auto truncate_impl(u64 size) -> VoidResult override {
        storage.resize(size.value);
        return {};
    }
    auto file_size_impl() -> Result<u64> override { return u64{storage.size()}; }
};

auto make_row(TypeRegistry& registry, const Type& first_type, std::string_view first_text,
              const Type& second_type, std::string_view second_text, const Type& third_type,
              std::string_view third_text) -> std::vector<Value> {
    auto first = first_type.from_text(first_text);
    auto second = second_type.from_text(second_text);
    auto third = third_type.from_text(third_text);
    EXPECT_TRUE(first.has_value());
    EXPECT_TRUE(second.has_value());
    EXPECT_TRUE(third.has_value());
    return std::vector<Value>{{.type_oid = first_type.oid, .bytes = *first, .is_null = b8{false}},
                              {.type_oid = second_type.oid, .bytes = *second, .is_null = b8{false}},
                              {.type_oid = third_type.oid, .bytes = *third, .is_null = b8{false}}};
}

auto to_text(TypeRegistry& registry, const Value& value) -> std::string {
    auto type = registry.lookup(value.type_oid);
    EXPECT_TRUE(type.has_value());
    return (*type)->to_text(value.bytes);
}

}  // namespace

class EdbTableTest : public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(register_builtin_types(registry).has_value());
        ASSERT_TRUE(page_store.open(io, PageStoreConfig{.page_size = usize{512}}).has_value());
        ASSERT_TRUE(engine
                        .open(page_store,
                              EngineConfig{.page_size = usize{512}, .buffer_pool_pages = usize{4}})
                        .has_value());

        const auto int_type = registry.lookup("int32");
        const auto text_type = registry.lookup("text");
        const auto bool_type = registry.lookup("bool");
        ASSERT_TRUE(int_type.has_value());
        ASSERT_TRUE(text_type.has_value());
        ASSERT_TRUE(bool_type.has_value());

        table = std::make_unique<Table>(
            registry, engine,
            TableSchema{
                .relation_oid = u32{42},
                .name = "users",
                .columns = {
                    {.name = "id", .type_oid = (*int_type)->oid, .nullable = b8{false}},
                    {.name = "name", .type_oid = (*text_type)->oid, .nullable = b8{false}},
                    {.name = "active", .type_oid = (*bool_type)->oid, .nullable = b8{false}}}});
    }

    void TearDown() override { ASSERT_TRUE(engine.close().has_value()); }

    TypeRegistry registry;
    MockCatalogIOOps io;
    PageStore page_store;
    EdbHeapEngine engine;
    std::unique_ptr<Table> table;
};

TEST_F(EdbTableTest, InsertAndScanTypedRows) {
    const auto int_type = registry.lookup("int32");
    const auto text_type = registry.lookup("text");
    const auto bool_type = registry.lookup("bool");
    ASSERT_TRUE(int_type.has_value());
    ASSERT_TRUE(text_type.has_value());
    ASSERT_TRUE(bool_type.has_value());

    auto first = make_row(registry, **int_type, "1", **text_type, "alice", **bool_type, "true");
    auto second = make_row(registry, **int_type, "2", **text_type, "bob", **bool_type, "false");

    ASSERT_TRUE(table->insert(first).has_value());
    ASSERT_TRUE(table->insert(second).has_value());

    auto rows = table->scan();
    ASSERT_TRUE(rows.has_value());
    ASSERT_EQ(rows->size(), std::size_t{2});
    EXPECT_EQ(to_text(registry, (*rows)[0][0]), std::string{"1"});
    EXPECT_EQ(to_text(registry, (*rows)[0][1]), std::string{"alice"});
    EXPECT_EQ(to_text(registry, (*rows)[0][2]), std::string{"true"});
    EXPECT_EQ(to_text(registry, (*rows)[1][0]), std::string{"2"});
    EXPECT_EQ(to_text(registry, (*rows)[1][1]), std::string{"bob"});
    EXPECT_EQ(to_text(registry, (*rows)[1][2]), std::string{"false"});
}

TEST_F(EdbTableTest, SchemaIsExposed) {
    EXPECT_EQ(table->schema().relation_oid.value, u32{42}.value);
    EXPECT_EQ(table->schema().name, std::string{"users"});
    ASSERT_EQ(table->schema().columns.size(), std::size_t{3});
    EXPECT_EQ(table->schema().columns[0].name, std::string{"id"});
}

TEST_F(EdbTableTest, ScanRowsReturnsTupleIds) {
    const auto int_type = registry.lookup("int32");
    const auto text_type = registry.lookup("text");
    const auto bool_type = registry.lookup("bool");
    ASSERT_TRUE(int_type.has_value());
    ASSERT_TRUE(text_type.has_value());
    ASSERT_TRUE(bool_type.has_value());

    auto row = make_row(registry, **int_type, "1", **text_type, "alice", **bool_type, "true");
    auto inserted = table->insert(row);
    ASSERT_TRUE(inserted.has_value());

    auto rows = table->scan_rows();
    ASSERT_TRUE(rows.has_value());
    ASSERT_EQ(rows->size(), std::size_t{1});
    EXPECT_EQ((*rows)[0].id.page_id.value, inserted->page_id.value);
    EXPECT_EQ((*rows)[0].id.slot_idx.value, inserted->slot_idx.value);
}