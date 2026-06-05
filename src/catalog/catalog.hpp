#pragma once

// src/catalog/catalog.hpp

#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "catalog/table.hpp"
#include "storage/engine/engine_factory.hpp"
#include "storage/io/io_ops.hpp"
#include "storage/page/page_store.hpp"
#include "types/registry.hpp"
#include "utils/error.hpp"
#include "utils/primitives.hpp"

namespace edb {

struct CatalogType {
    u32 oid{0};
    std::string name;
    std::optional<usize> fixed_size;
};

struct CatalogClass {
    u32 oid{0};
    std::string name;
    std::string relkind;
};

struct CatalogAttribute {
    u32 relation_oid{0};
    u32 attnum{0};
    std::string name;
    u32 type_oid{0};
    b8 nullable{false};
};

struct CreateTableSpec {
    std::string name;
    std::vector<ColumnSchema> columns;
};

class RelationBackendFactory {
   public:
    RelationBackendFactory() = default;
    RelationBackendFactory(const RelationBackendFactory&) = delete;
    RelationBackendFactory& operator=(const RelationBackendFactory&) = delete;
    RelationBackendFactory(RelationBackendFactory&&) = delete;
    RelationBackendFactory& operator=(RelationBackendFactory&&) = delete;
    virtual ~RelationBackendFactory() = default;

    [[nodiscard]] virtual auto open_backend(u32 relation_oid, std::string_view relation_name)
        -> Result<std::unique_ptr<StorageIOOps>> = 0;
};

class Catalog {
   public:
    Catalog(const TypeRegistry& registry, RelationBackendFactory& backend_factory,
            const EngineConfig& engine_config);
    Catalog(const TypeRegistry& registry, RelationBackendFactory& backend_factory,
            StorageEngineFactory& engine_factory, const EngineConfig& engine_config);

    Catalog(const Catalog&) = delete;
    Catalog& operator=(const Catalog&) = delete;
    Catalog(Catalog&&) = delete;
    Catalog& operator=(Catalog&&) = delete;
    ~Catalog() = default;

    auto open() -> VoidResult;
    auto close() -> VoidResult;

    // Attach a WAL emitter so that all subsequent relation mutations emit WAL
    // records. Must be called before the first SQL statement if durability is
    // required. Safe to call when no tables are open.
    auto set_wal_emitter(WalEmitter& wal_emitter) noexcept -> void;

    [[nodiscard]] auto get_type(std::string_view name) -> Result<CatalogType>;
    [[nodiscard]] auto get_class(std::string_view name) -> Result<CatalogClass>;
    [[nodiscard]] auto get_attributes(u32 class_oid) -> Result<std::vector<CatalogAttribute>>;

    [[nodiscard]] auto create_table(const CreateTableSpec& spec) -> Result<u32>;
    auto drop_table(u32 class_oid) -> VoidResult;

    [[nodiscard]] auto open_table(std::string_view name) -> Result<Table*>;

   private:
    struct OpenedTableBundle {
        std::unique_ptr<StorageIOOps> backend;
        PageStore page_store;
        std::unique_ptr<StorageEngineOps> engine;
        std::unique_ptr<Table> table;

        auto open(const TypeRegistry& registry, RelationBackendFactory& backend_factory,
                  StorageEngineFactory& engine_factory, const TableSchema& schema,
                  const EngineConfig& engine_config) -> VoidResult;
        auto close() -> VoidResult;
    };

    [[nodiscard]] auto check_open() const -> VoidResult;
    auto open_system_tables() -> VoidResult;
    auto bootstrap_if_needed() -> VoidResult;
    auto bootstrap_catalog() -> VoidResult;
    auto bootstrap_types() -> VoidResult;
    auto bootstrap_classes() -> VoidResult;
    auto bootstrap_attributes() -> VoidResult;
    auto ensure_user_table_open(u32 relation_oid, std::string_view relation_name,
                                std::span<const CatalogAttribute> attributes) -> Result<Table*>;
    auto lookup_class_row(u32 class_oid) -> Result<std::optional<TableRow>>;
    auto lookup_attribute_rows(u32 class_oid) -> Result<std::vector<TableRow>>;
    auto next_relation_oid() -> Result<u32>;

    [[nodiscard]] auto decode_type(const TableRow& row) -> Result<CatalogType>;
    [[nodiscard]] auto decode_class(const TableRow& row) -> Result<CatalogClass>;
    [[nodiscard]] auto decode_attribute(const TableRow& row) -> Result<CatalogAttribute>;

    [[nodiscard]] auto make_int32_value(i32 value) const -> Result<Value>;
    [[nodiscard]] auto make_text_value(std::string_view text) const -> Result<Value>;
    [[nodiscard]] auto make_bool_value(b8 value) const -> Result<Value>;

    [[nodiscard]] auto parse_i32(const Value& value) const -> Result<i32>;
    [[nodiscard]] auto parse_text(const Value& value) const -> Result<std::string>;
    [[nodiscard]] auto parse_bool(const Value& value) const -> Result<b8>;

    auto invalidate_type_cache() -> void;
    auto invalidate_class_cache(u32 class_oid, std::string_view class_name) -> void;

    const TypeRegistry* types{nullptr};
    RelationBackendFactory* backends{nullptr};
    std::unique_ptr<StorageEngineFactory> owned_engines;
    StorageEngineFactory* engines{nullptr};
    EngineConfig config{};
    b8 opened{false};
    mutable std::recursive_mutex latch;

    OpenedTableBundle type_table{};
    OpenedTableBundle class_table{};
    OpenedTableBundle attribute_table{};
    OpenedTableBundle index_table{};
    std::unordered_map<u32, std::unique_ptr<OpenedTableBundle>> user_tables;

    std::unordered_map<std::string, CatalogType> type_cache_by_name;
    std::unordered_map<std::string, CatalogClass> class_cache_by_name;
    std::unordered_map<u32, std::vector<CatalogAttribute>> attribute_cache_by_relid;
};

}  // namespace edb