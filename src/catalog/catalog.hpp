#pragma once

// src/catalog/catalog.hpp

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "catalog/table.hpp"
#include "storage/engine/heap/heap_engine.hpp"
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
    std::vector<EdbColumnSchema> columns;
};

class EdbRelationBackendFactory {
   public:
    EdbRelationBackendFactory() = default;
    EdbRelationBackendFactory(const EdbRelationBackendFactory&) = delete;
    EdbRelationBackendFactory& operator=(const EdbRelationBackendFactory&) = delete;
    EdbRelationBackendFactory(EdbRelationBackendFactory&&) = delete;
    EdbRelationBackendFactory& operator=(EdbRelationBackendFactory&&) = delete;
    virtual ~EdbRelationBackendFactory() = default;

    [[nodiscard]] virtual auto open_backend(u32 relation_oid, std::string_view relation_name)
        -> EdbResult<std::unique_ptr<EdbStorageIOOps>> = 0;
};

class EdbCatalog {
   public:
    EdbCatalog(const EdbTypeRegistry& registry, EdbRelationBackendFactory& backend_factory,
               const EdbEngineConfig& engine_config);

    EdbCatalog(const EdbCatalog&) = delete;
    EdbCatalog& operator=(const EdbCatalog&) = delete;
    EdbCatalog(EdbCatalog&&) = delete;
    EdbCatalog& operator=(EdbCatalog&&) = delete;
    ~EdbCatalog() = default;

    auto open() -> EdbStatus;
    auto close() -> EdbStatus;

    [[nodiscard]] auto get_type(std::string_view name) -> EdbResult<CatalogType>;
    [[nodiscard]] auto get_class(std::string_view name) -> EdbResult<CatalogClass>;
    [[nodiscard]] auto get_attributes(u32 class_oid) -> EdbResult<std::vector<CatalogAttribute>>;

    [[nodiscard]] auto create_table(const CreateTableSpec& spec) -> EdbResult<u32>;
    auto drop_table(u32 class_oid) -> EdbStatus;

    [[nodiscard]] auto open_table(std::string_view name) -> EdbResult<EdbTable*>;

   private:
    struct OpenedTableBundle {
        std::unique_ptr<EdbStorageIOOps> backend;
        EdbPageStore page_store;
        EdbHeapEngine engine;
        std::unique_ptr<EdbTable> table;

        auto open(const EdbTypeRegistry& registry, EdbRelationBackendFactory& factory,
                  const EdbTableSchema& schema, const EdbEngineConfig& engine_config) -> EdbStatus;
        auto close() -> EdbStatus;
    };

    [[nodiscard]] auto check_open() const -> EdbStatus;
    auto open_system_tables() -> EdbStatus;
    auto bootstrap_if_needed() -> EdbStatus;
    auto bootstrap_catalog() -> EdbStatus;
    auto bootstrap_types() -> EdbStatus;
    auto bootstrap_classes() -> EdbStatus;
    auto bootstrap_attributes() -> EdbStatus;
    auto ensure_user_table_open(u32 relation_oid, std::string_view relation_name,
                                std::span<const CatalogAttribute> attributes)
        -> EdbResult<EdbTable*>;
    auto lookup_class_row(u32 class_oid) -> EdbResult<std::optional<EdbTableRow>>;
    auto lookup_attribute_rows(u32 class_oid) -> EdbResult<std::vector<EdbTableRow>>;
    auto next_relation_oid() -> EdbResult<u32>;

    [[nodiscard]] auto decode_type(const EdbTableRow& row) -> EdbResult<CatalogType>;
    [[nodiscard]] auto decode_class(const EdbTableRow& row) -> EdbResult<CatalogClass>;
    [[nodiscard]] auto decode_attribute(const EdbTableRow& row) -> EdbResult<CatalogAttribute>;

    [[nodiscard]] auto make_int32_value(i32 value) const -> EdbResult<EdbValue>;
    [[nodiscard]] auto make_text_value(std::string_view text) const -> EdbResult<EdbValue>;
    [[nodiscard]] auto make_bool_value(b8 value) const -> EdbResult<EdbValue>;

    [[nodiscard]] auto parse_i32(const EdbValue& value) const -> EdbResult<i32>;
    [[nodiscard]] auto parse_text(const EdbValue& value) const -> EdbResult<std::string>;
    [[nodiscard]] auto parse_bool(const EdbValue& value) const -> EdbResult<b8>;

    auto invalidate_type_cache() -> void;
    auto invalidate_class_cache(u32 class_oid, std::string_view class_name) -> void;

    const EdbTypeRegistry* types{nullptr};
    EdbRelationBackendFactory* backends{nullptr};
    EdbEngineConfig config{};
    b8 opened{false};

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