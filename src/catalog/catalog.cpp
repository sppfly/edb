// src/catalog/catalog.cpp

#include "catalog/catalog.hpp"

#include <algorithm>
#include <expected>
#include <utility>

namespace edb {

namespace {

constexpr auto EDB_TYPE_RELATION_OID = u32{1};
constexpr auto EDB_CLASS_RELATION_OID = u32{2};
constexpr auto EDB_ATTRIBUTE_RELATION_OID = u32{3};
constexpr auto EDB_INDEX_RELATION_OID = u32{4};

constexpr auto RELKIND_SYSTEM = std::string_view{"system"};
constexpr auto RELKIND_TABLE = std::string_view{"table"};
constexpr auto RELKIND_INDEX = std::string_view{"index"};

auto fixed_size_to_catalog_length(const std::optional<usize>& fixed_size) -> i32 {
    if (!fixed_size.has_value()) {
        return i32{-1};
    }
    return i32{static_cast<std::int32_t>(
        fixed_size->value)};  // raw-primitive: catalog length stored as int32
}

auto catalog_length_to_fixed_size(i32 length) -> std::optional<usize> {
    if (length.value < 0) {
        return std::nullopt;
    }
    return usize{
        static_cast<std::size_t>(length.value)};  // raw-primitive: catalog int32 converted to size
}

auto compare_attnum(const CatalogAttribute& lhs, const CatalogAttribute& rhs) -> bool {
    return lhs.attnum < rhs.attnum;
}

}  // namespace

EdbCatalog::EdbCatalog(const EdbTypeRegistry& registry, EdbRelationBackendFactory& backend_factory,
                       const EdbEngineConfig& engine_config)
    : types{&registry}, backends{&backend_factory}, config{engine_config} {}

auto EdbCatalog::OpenedTableBundle::open(const EdbTypeRegistry& registry,
                                         EdbRelationBackendFactory& factory,
                                         const EdbTableSchema& schema,
                                         const EdbEngineConfig& engine_config) -> EdbStatus {
    auto opened_backend = factory.open_backend(schema.relation_oid, schema.name);
    if (!opened_backend) {
        return std::unexpected(opened_backend.error());
    }

    backend = std::move(*opened_backend);
    auto page_status =
        page_store.open(*backend, EdbPageStoreConfig{.page_size = engine_config.page_size});
    if (!page_status) {
        backend.reset();
        return page_status;
    }

    auto engine_status = engine.open(page_store, engine_config);
    if (!engine_status) {
        if (auto page_status = page_store.close(); !page_status) {
            return page_status;
        }
        backend.reset();
        return engine_status;
    }

    table = std::make_unique<EdbTable>(registry, engine, schema);
    return {};
}

auto EdbCatalog::OpenedTableBundle::close() -> EdbStatus {
    table.reset();

    auto engine_status = engine.close();
    if (!engine_status) {
        return engine_status;
    }
    auto page_status = page_store.close();
    if (!page_status) {
        return page_status;
    }
    if (backend != nullptr) {
        auto backend_status = backend->close();
        if (!backend_status) {
            return backend_status;
        }
        backend.reset();
    }
    return {};
}

auto EdbCatalog::open() -> EdbStatus {
    if (auto status = open_system_tables(); !status) {
        return status;
    }
    if (auto status = bootstrap_if_needed(); !status) {
        return status;
    }
    opened = b8{true};
    return {};
}

auto EdbCatalog::close() -> EdbStatus {
    for (auto& [relation_oid, table] : user_tables) {
        (void)relation_oid;
        if (auto status = table->close(); !status) {
            return status;
        }
    }
    user_tables.clear();

    if (auto status = index_table.close(); !status) {
        return status;
    }
    if (auto status = attribute_table.close(); !status) {
        return status;
    }
    if (auto status = class_table.close(); !status) {
        return status;
    }
    if (auto status = type_table.close(); !status) {
        return status;
    }

    invalidate_type_cache();
    class_cache_by_name.clear();
    attribute_cache_by_relid.clear();
    opened = b8{false};
    return {};
}

auto EdbCatalog::check_open() const -> EdbStatus {
    if (!opened.value) {
        return std::unexpected(EdbError::InvalidArgument);
    }
    return {};
}

auto EdbCatalog::open_system_tables() -> EdbStatus {
    const auto int32_type = types->lookup("int32");
    const auto text_type = types->lookup("text");
    const auto bool_type = types->lookup("bool");
    if (!int32_type || !text_type || !bool_type) {
        return std::unexpected(EdbError::TypeNotFound);
    }

    const EdbTableSchema type_schema{
        .relation_oid = EDB_TYPE_RELATION_OID,
        .name = "edb_type",
        .columns = {{.name = "oid", .type_oid = (*int32_type)->oid, .nullable = b8{false}},
                    {.name = "name", .type_oid = (*text_type)->oid, .nullable = b8{false}},
                    {.name = "typlen", .type_oid = (*int32_type)->oid, .nullable = b8{false}}}};
    const EdbTableSchema class_schema{
        .relation_oid = EDB_CLASS_RELATION_OID,
        .name = "edb_class",
        .columns = {{.name = "oid", .type_oid = (*int32_type)->oid, .nullable = b8{false}},
                    {.name = "name", .type_oid = (*text_type)->oid, .nullable = b8{false}},
                    {.name = "relkind", .type_oid = (*text_type)->oid, .nullable = b8{false}}}};
    const EdbTableSchema attribute_schema{
        .relation_oid = EDB_ATTRIBUTE_RELATION_OID,
        .name = "edb_attribute",
        .columns = {{.name = "attrelid", .type_oid = (*int32_type)->oid, .nullable = b8{false}},
                    {.name = "attnum", .type_oid = (*int32_type)->oid, .nullable = b8{false}},
                    {.name = "name", .type_oid = (*text_type)->oid, .nullable = b8{false}},
                    {.name = "type_oid", .type_oid = (*int32_type)->oid, .nullable = b8{false}},
                    {.name = "nullable", .type_oid = (*bool_type)->oid, .nullable = b8{false}}}};
    const EdbTableSchema index_schema{
        .relation_oid = EDB_INDEX_RELATION_OID,
        .name = "edb_index",
        .columns = {{.name = "oid", .type_oid = (*int32_type)->oid, .nullable = b8{false}},
                    {.name = "indrelid", .type_oid = (*int32_type)->oid, .nullable = b8{false}},
                    {.name = "am_name", .type_oid = (*text_type)->oid, .nullable = b8{false}}}};

    if (auto status = type_table.open(*types, *backends, type_schema, config); !status) {
        return status;
    }
    if (auto status = class_table.open(*types, *backends, class_schema, config); !status) {
        return status;
    }
    if (auto status = attribute_table.open(*types, *backends, attribute_schema, config); !status) {
        return status;
    }
    return index_table.open(*types, *backends, index_schema, config);
}

auto EdbCatalog::bootstrap_if_needed() -> EdbStatus {
    auto classes = class_table.table->scan_rows();
    if (!classes) {
        return std::unexpected(classes.error());
    }
    if (!classes->empty()) {
        return {};
    }
    return bootstrap_catalog();
}

auto EdbCatalog::bootstrap_catalog() -> EdbStatus {
    if (auto status = bootstrap_types(); !status) {
        return status;
    }
    if (auto status = bootstrap_classes(); !status) {
        return status;
    }
    return bootstrap_attributes();
}

auto EdbCatalog::bootstrap_types() -> EdbStatus {
    for (const auto* const name : {"int32", "int64", "float64", "bool", "text"}) {
        auto builtin = types->lookup(name);
        if (!builtin) {
            return std::unexpected(builtin.error());
        }

        auto oid = make_int32_value(i32{static_cast<std::int32_t>((*builtin)->oid.value)});
        auto type_name = make_text_value((*builtin)->name);
        auto type_length = make_int32_value(fixed_size_to_catalog_length((*builtin)->fixed_size));
        if (!oid || !type_name || !type_length) {
            return std::unexpected(EdbError::TypeNotFound);
        }

        auto status =
            type_table.table->insert(std::vector<EdbValue>{*oid, *type_name, *type_length});
        if (!status) {
            return std::unexpected(status.error());
        }
    }
    return {};
}

auto EdbCatalog::bootstrap_classes() -> EdbStatus {
    const auto system_rows = std::array<std::pair<u32, std::string_view>, 4>{
        {{EDB_TYPE_RELATION_OID, "edb_type"},
         {EDB_CLASS_RELATION_OID, "edb_class"},
         {EDB_ATTRIBUTE_RELATION_OID, "edb_attribute"},
         {EDB_INDEX_RELATION_OID, "edb_index"}}};

    for (const auto& [oid_value, name] : system_rows) {
        auto oid = make_int32_value(i32{static_cast<std::int32_t>(oid_value.value)});
        auto class_name = make_text_value(name);
        auto relkind =
            make_text_value(name == std::string_view{"edb_index"} ? RELKIND_INDEX : RELKIND_SYSTEM);
        if (!oid || !class_name || !relkind) {
            return std::unexpected(EdbError::TypeNotFound);
        }

        auto status = class_table.table->insert(std::vector<EdbValue>{*oid, *class_name, *relkind});
        if (!status) {
            return std::unexpected(status.error());
        }
    }
    return {};
}

auto EdbCatalog::bootstrap_attributes() -> EdbStatus {
    const auto class_scan = class_table.table->scan_rows();
    if (!class_scan) {
        return std::unexpected(class_scan.error());
    }

    auto write_schema_attributes =
        [this](u32 relation_oid, const std::vector<EdbColumnSchema>& columns) -> EdbStatus {
        for (usize index{0}; index < usize{columns.size()}; ++index) {
            const auto& column = columns[index.value];
            auto relid = make_int32_value(i32{static_cast<std::int32_t>(relation_oid.value)});
            auto attnum = make_int32_value(i32{static_cast<std::int32_t>(index.value + 1U)});
            auto name = make_text_value(column.name);
            auto type_oid = make_int32_value(i32{static_cast<std::int32_t>(column.type_oid.value)});
            auto nullable = make_bool_value(column.nullable);
            if (!relid || !attnum || !name || !type_oid || !nullable) {
                return std::unexpected(EdbError::TypeNotFound);
            }
            auto inserted = attribute_table.table->insert(
                std::vector<EdbValue>{*relid, *attnum, *name, *type_oid, *nullable});
            if (!inserted) {
                return std::unexpected(inserted.error());
            }
        }
        return {};
    };

    if (auto status =
            write_schema_attributes(EDB_TYPE_RELATION_OID, type_table.table->schema().columns);
        !status) {
        return status;
    }
    if (auto status =
            write_schema_attributes(EDB_CLASS_RELATION_OID, class_table.table->schema().columns);
        !status) {
        return status;
    }
    if (auto status = write_schema_attributes(EDB_ATTRIBUTE_RELATION_OID,
                                              attribute_table.table->schema().columns);
        !status) {
        return status;
    }
    return write_schema_attributes(EDB_INDEX_RELATION_OID, index_table.table->schema().columns);
}

auto EdbCatalog::decode_type(const EdbTableRow& row) -> EdbResult<CatalogType> {
    if (row.values.size() != 3U) {
        return std::unexpected(EdbError::Corruption);
    }
    const auto oid = parse_i32(row.values[0]);
    const auto name = parse_text(row.values[1]);
    const auto length = parse_i32(row.values[2]);
    if (!oid || !name || !length || oid->value < 0) {
        return std::unexpected(EdbError::Corruption);
    }
    return CatalogType{.oid = u32{static_cast<std::uint32_t>(oid->value)},
                       .name = *name,
                       .fixed_size = catalog_length_to_fixed_size(*length)};
}

auto EdbCatalog::decode_class(const EdbTableRow& row) -> EdbResult<CatalogClass> {
    if (row.values.size() != 3U) {
        return std::unexpected(EdbError::Corruption);
    }
    const auto oid = parse_i32(row.values[0]);
    const auto name = parse_text(row.values[1]);
    const auto relkind = parse_text(row.values[2]);
    if (!oid || !name || !relkind || oid->value < 0) {
        return std::unexpected(EdbError::Corruption);
    }
    return CatalogClass{
        .oid = u32{static_cast<std::uint32_t>(oid->value)}, .name = *name, .relkind = *relkind};
}

auto EdbCatalog::decode_attribute(const EdbTableRow& row) -> EdbResult<CatalogAttribute> {
    if (row.values.size() != 5U) {
        return std::unexpected(EdbError::Corruption);
    }
    const auto relid = parse_i32(row.values[0]);
    const auto attnum = parse_i32(row.values[1]);
    const auto name = parse_text(row.values[2]);
    const auto type_oid = parse_i32(row.values[3]);
    const auto nullable = parse_bool(row.values[4]);
    if (!relid || !attnum || !name || !type_oid || !nullable || relid->value < 0 ||
        attnum->value < 0 || type_oid->value < 0) {
        return std::unexpected(EdbError::Corruption);
    }
    return CatalogAttribute{.relation_oid = u32{static_cast<std::uint32_t>(relid->value)},
                            .attnum = u32{static_cast<std::uint32_t>(attnum->value)},
                            .name = *name,
                            .type_oid = u32{static_cast<std::uint32_t>(type_oid->value)},
                            .nullable = *nullable};
}

auto EdbCatalog::make_int32_value(i32 value) const -> EdbResult<EdbValue> {
    auto type = types->lookup("int32");
    if (!type) {
        return std::unexpected(type.error());
    }
    auto bytes = (*type)->from_text(std::to_string(value.value));
    if (!bytes) {
        return std::unexpected(bytes.error());
    }
    return EdbValue{.type_oid = (*type)->oid, .bytes = *bytes, .is_null = b8{false}};
}

auto EdbCatalog::make_text_value(std::string_view text) const -> EdbResult<EdbValue> {
    auto type = types->lookup("text");
    if (!type) {
        return std::unexpected(type.error());
    }
    auto bytes = (*type)->from_text(text);
    if (!bytes) {
        return std::unexpected(bytes.error());
    }
    return EdbValue{.type_oid = (*type)->oid, .bytes = *bytes, .is_null = b8{false}};
}

auto EdbCatalog::make_bool_value(b8 value) const -> EdbResult<EdbValue> {
    auto type = types->lookup("bool");
    if (!type) {
        return std::unexpected(type.error());
    }
    auto bytes = (*type)->from_text(value.value ? "true" : "false");
    if (!bytes) {
        return std::unexpected(bytes.error());
    }
    return EdbValue{.type_oid = (*type)->oid, .bytes = *bytes, .is_null = b8{false}};
}

auto EdbCatalog::parse_i32(const EdbValue& value) const -> EdbResult<i32> {
    auto type = types->lookup(value.type_oid);
    if (!type) {
        return std::unexpected(EdbError::TypeNotFound);
    }
    const auto text = (*type)->to_text(value.bytes);
    try {
        return i32{std::stoi(text)};  // raw-primitive: std::stoi parses primitive scalar
    } catch (...) {
        return std::unexpected(EdbError::Corruption);
    }
}

auto EdbCatalog::parse_text(const EdbValue& value) const -> EdbResult<std::string> {
    auto type = types->lookup(value.type_oid);
    if (!type) {
        return std::unexpected(EdbError::TypeNotFound);
    }
    return (*type)->to_text(value.bytes);
}

auto EdbCatalog::parse_bool(const EdbValue& value) const -> EdbResult<b8> {
    auto type = types->lookup(value.type_oid);
    if (!type) {
        return std::unexpected(EdbError::TypeNotFound);
    }
    const auto text = (*type)->to_text(value.bytes);
    if (text == "true") {
        return b8{true};
    }
    if (text == "false") {
        return b8{false};
    }
    return std::unexpected(EdbError::Corruption);
}

auto EdbCatalog::invalidate_type_cache() -> void {
    type_cache_by_name.clear();
}

auto EdbCatalog::invalidate_class_cache(u32 class_oid, std::string_view class_name) -> void {
    class_cache_by_name.erase(std::string{class_name});
    attribute_cache_by_relid.erase(class_oid);
}

auto EdbCatalog::get_type(std::string_view name) -> EdbResult<CatalogType> {
    if (auto status = check_open(); !status) {
        return std::unexpected(status.error());
    }
    const auto found = type_cache_by_name.find(std::string{name});
    if (found != type_cache_by_name.end()) {
        return found->second;
    }

    auto rows = type_table.table->scan_rows();
    if (!rows) {
        return std::unexpected(rows.error());
    }
    for (const auto& row : *rows) {
        auto decoded = decode_type(row);
        if (!decoded) {
            return std::unexpected(decoded.error());
        }
        if (decoded->name == name) {
            type_cache_by_name.emplace(decoded->name, *decoded);
            return *decoded;
        }
    }
    return std::unexpected(EdbError::NotFound);
}

auto EdbCatalog::get_class(std::string_view name) -> EdbResult<CatalogClass> {
    if (auto status = check_open(); !status) {
        return std::unexpected(status.error());
    }
    const auto found = class_cache_by_name.find(std::string{name});
    if (found != class_cache_by_name.end()) {
        return found->second;
    }

    auto rows = class_table.table->scan_rows();
    if (!rows) {
        return std::unexpected(rows.error());
    }
    for (const auto& row : *rows) {
        auto decoded = decode_class(row);
        if (!decoded) {
            return std::unexpected(decoded.error());
        }
        if (decoded->name == name) {
            class_cache_by_name.emplace(decoded->name, *decoded);
            return *decoded;
        }
    }
    return std::unexpected(EdbError::NotFound);
}

auto EdbCatalog::get_attributes(u32 class_oid) -> EdbResult<std::vector<CatalogAttribute>> {
    if (auto status = check_open(); !status) {
        return std::unexpected(status.error());
    }
    const auto found = attribute_cache_by_relid.find(class_oid);
    if (found != attribute_cache_by_relid.end()) {
        return found->second;
    }

    auto rows = attribute_table.table->scan_rows();
    if (!rows) {
        return std::unexpected(rows.error());
    }
    std::vector<CatalogAttribute> attributes;
    for (const auto& row : *rows) {
        auto decoded = decode_attribute(row);
        if (!decoded) {
            return std::unexpected(decoded.error());
        }
        if (decoded->relation_oid == class_oid) {
            attributes.push_back(*decoded);
        }
    }
    std::ranges::sort(attributes, compare_attnum);
    if (attributes.empty()) {
        return std::unexpected(EdbError::NotFound);
    }
    attribute_cache_by_relid.emplace(class_oid, attributes);
    return attributes;
}

auto EdbCatalog::next_relation_oid() -> EdbResult<u32> {
    auto rows = class_table.table->scan_rows();
    if (!rows) {
        return std::unexpected(rows.error());
    }

    u32 max_oid{0};
    for (const auto& row : *rows) {
        auto decoded = decode_class(row);
        if (!decoded) {
            return std::unexpected(decoded.error());
        }
        if (decoded->oid > max_oid) {
            max_oid = decoded->oid;
        }
    }
    return u32{max_oid.value + 1U};
}

auto EdbCatalog::create_table(const CreateTableSpec& spec) -> EdbResult<u32> {
    if (auto status = check_open(); !status) {
        return std::unexpected(status.error());
    }
    if (spec.name.empty() || spec.columns.empty()) {
        return std::unexpected(EdbError::InvalidArgument);
    }
    auto existing = get_class(spec.name);
    if (existing.has_value()) {
        return std::unexpected(EdbError::AlreadyExists);
    }
    if (existing.error() != EdbError::NotFound) {
        return std::unexpected(existing.error());
    }

    for (const auto& column : spec.columns) {
        if (!types->lookup(column.type_oid)) {
            return std::unexpected(EdbError::TypeNotFound);
        }
    }

    auto relation_oid = next_relation_oid();
    if (!relation_oid) {
        return std::unexpected(relation_oid.error());
    }

    auto class_oid = make_int32_value(i32{static_cast<std::int32_t>(relation_oid->value)});
    auto class_name = make_text_value(spec.name);
    auto relkind = make_text_value(RELKIND_TABLE);
    if (!class_oid || !class_name || !relkind) {
        return std::unexpected(EdbError::TypeNotFound);
    }
    auto inserted =
        class_table.table->insert(std::vector<EdbValue>{*class_oid, *class_name, *relkind});
    if (!inserted) {
        return std::unexpected(inserted.error());
    }

    for (usize index{0}; index < usize{spec.columns.size()}; ++index) {
        const auto& column = spec.columns[index.value];
        auto relid = make_int32_value(i32{static_cast<std::int32_t>(relation_oid->value)});
        auto attnum = make_int32_value(i32{static_cast<std::int32_t>(index.value + 1U)});
        auto name = make_text_value(column.name);
        auto type_oid = make_int32_value(i32{static_cast<std::int32_t>(column.type_oid.value)});
        auto nullable = make_bool_value(column.nullable);
        if (!relid || !attnum || !name || !type_oid || !nullable) {
            return std::unexpected(EdbError::TypeNotFound);
        }
        auto attr_inserted = attribute_table.table->insert(
            std::vector<EdbValue>{*relid, *attnum, *name, *type_oid, *nullable});
        if (!attr_inserted) {
            return std::unexpected(attr_inserted.error());
        }
    }

    invalidate_class_cache(*relation_oid, spec.name);
    auto opened_table =
        ensure_user_table_open(*relation_oid, spec.name, std::span<const CatalogAttribute>{});
    if (!opened_table) {
        return std::unexpected(opened_table.error());
    }
    return *relation_oid;
}

auto EdbCatalog::lookup_class_row(u32 class_oid) -> EdbResult<std::optional<EdbTableRow>> {
    auto rows = class_table.table->scan_rows();
    if (!rows) {
        return std::unexpected(rows.error());
    }
    for (const auto& row : *rows) {
        auto decoded = decode_class(row);
        if (!decoded) {
            return std::unexpected(decoded.error());
        }
        if (decoded->oid == class_oid) {
            return row;
        }
    }
    return std::optional<EdbTableRow>{};
}

auto EdbCatalog::lookup_attribute_rows(u32 class_oid) -> EdbResult<std::vector<EdbTableRow>> {
    auto rows = attribute_table.table->scan_rows();
    if (!rows) {
        return std::unexpected(rows.error());
    }

    std::vector<EdbTableRow> matched;
    for (const auto& row : *rows) {
        auto decoded = decode_attribute(row);
        if (!decoded) {
            return std::unexpected(decoded.error());
        }
        if (decoded->relation_oid == class_oid) {
            matched.push_back(row);
        }
    }
    return matched;
}

auto EdbCatalog::drop_table(u32 class_oid) -> EdbStatus {
    if (auto status = check_open(); !status) {
        return status;
    }

    auto class_row = lookup_class_row(class_oid);
    if (!class_row) {
        return std::unexpected(class_row.error());
    }
    if (!class_row->has_value()) {
        return std::unexpected(EdbError::NotFound);
    }

    auto decoded_class = decode_class(**class_row);
    if (!decoded_class) {
        return std::unexpected(decoded_class.error());
    }
    if (decoded_class->relkind != std::string{RELKIND_TABLE}) {
        return std::unexpected(EdbError::InvalidArgument);
    }

    auto attribute_rows = lookup_attribute_rows(class_oid);
    if (!attribute_rows) {
        return std::unexpected(attribute_rows.error());
    }

    for (const auto& row : *attribute_rows) {
        auto status = attribute_table.engine.delete_tuple(row.id);
        if (!status) {
            return status;
        }
    }
    auto class_status = class_table.engine.delete_tuple((**class_row).id);
    if (!class_status) {
        return class_status;
    }

    user_tables.erase(class_oid);
    invalidate_class_cache(class_oid, decoded_class->name);
    return {};
}

auto EdbCatalog::ensure_user_table_open(u32 relation_oid, std::string_view relation_name,
                                        std::span<const CatalogAttribute> attributes)
    -> EdbResult<EdbTable*> {
    const auto opened_table = user_tables.find(relation_oid);
    if (opened_table != user_tables.end()) {
        return opened_table->second->table.get();
    }

    std::vector<CatalogAttribute> resolved_attributes;
    if (!attributes.empty()) {
        resolved_attributes.assign(attributes.begin(), attributes.end());
    } else {
        auto loaded_attributes = get_attributes(relation_oid);
        if (!loaded_attributes) {
            return std::unexpected(loaded_attributes.error());
        }
        resolved_attributes = *loaded_attributes;
    }
    std::ranges::sort(resolved_attributes, compare_attnum);

    EdbTableSchema schema{
        .relation_oid = relation_oid, .name = std::string{relation_name}, .columns = {}};
    schema.columns.reserve(resolved_attributes.size());
    for (const auto& attribute : resolved_attributes) {
        schema.columns.push_back(EdbColumnSchema{.name = attribute.name,
                                                 .type_oid = attribute.type_oid,
                                                 .nullable = attribute.nullable});
    }

    auto bundle = std::make_unique<OpenedTableBundle>();
    auto status = bundle->open(*types, *backends, schema, config);
    if (!status) {
        return std::unexpected(status.error());
    }

    auto* table_ptr = bundle->table.get();
    user_tables.emplace(relation_oid, std::move(bundle));
    return table_ptr;
}

auto EdbCatalog::open_table(std::string_view name) -> EdbResult<EdbTable*> {
    if (auto status = check_open(); !status) {
        return std::unexpected(status.error());
    }

    auto klass = get_class(name);
    if (!klass) {
        return std::unexpected(klass.error());
    }
    return ensure_user_table_open(klass->oid, klass->name, std::span<const CatalogAttribute>{});
}

}  // namespace edb