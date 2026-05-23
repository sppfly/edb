// src/types/row_codec.cpp

#include "types/row_codec.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>

namespace edb {

namespace {

constexpr auto COLUMN_COUNT_BYTES = std::size_t{4};
constexpr auto OFFSET_BYTES = std::size_t{4};
constexpr auto LENGTH_BYTES = std::size_t{4};

auto append_u32_le(std::vector<std::byte>& out, std::uint32_t value) -> void {
    constexpr auto byte_count = std::size_t{4};
    for (std::size_t index = 0; index < byte_count; ++index) {  // raw-primitive: byte encoding
        const auto shift = static_cast<unsigned>(index * 8U);   // raw-primitive: bit shift count
        out.push_back(
            std::byte{static_cast<unsigned char>((value >> shift) & std::uint32_t{0xFFU})});
    }
}

auto read_u32_le(std::span<const std::byte> bytes, usize offset) -> EdbResult<std::uint32_t> {
    if ((offset.value + COLUMN_COUNT_BYTES) > bytes.size()) {
        return std::unexpected(EdbError::Corruption);
    }

    std::uint32_t value{0};
    for (std::size_t index = 0; index < COLUMN_COUNT_BYTES;
         ++index) {  // raw-primitive: byte decode
        value |=
            static_cast<std::uint32_t>(std::to_integer<unsigned char>(bytes[offset.value + index]))
            << static_cast<unsigned>(index * 8U);
    }
    return value;
}

auto bitmap_bytes_for_columns(usize column_count) -> usize {
    return usize{(column_count.value + 7U) / 8U};
}

auto is_null_bit_set(std::span<const std::byte> bitmap, usize index) -> b8 {
    const auto byte_index = index.value / 8U;  // raw-primitive: bitmap addressing
    const auto bit_index = index.value % 8U;   // raw-primitive: bitmap addressing
    const auto mask = static_cast<unsigned char>(1U << bit_index);
    return b8{(std::to_integer<unsigned char>(bitmap[byte_index]) & mask) != 0U};
}

auto set_null_bit(std::span<std::byte> bitmap, usize index) -> void {
    const auto byte_index = index.value / 8U;  // raw-primitive: bitmap addressing
    const auto bit_index = index.value % 8U;   // raw-primitive: bitmap addressing
    const auto mask = static_cast<unsigned char>(1U << bit_index);
    bitmap[byte_index] |= std::byte{mask};
}

}  // namespace

EdbRowCodec::EdbRowCodec(const EdbTypeRegistry& registry_ref,
                         std::vector<EdbColumnSchema> schema_def)
    : registry{&registry_ref}, schema{std::move(schema_def)} {}

auto EdbRowCodec::encode(std::span<const EdbValue> values) const
    -> EdbResult<std::vector<std::byte>> {
    if (values.size() != schema.size()) {
        return std::unexpected(EdbError::InvalidArgument);
    }
    if (registry == nullptr) {
        return std::unexpected(EdbError::InvalidArgument);
    }

    const auto column_count = usize{schema.size()};
    const auto bitmap_size = bitmap_bytes_for_columns(column_count);
    const auto entries_size = usize{column_count.value * (OFFSET_BYTES + LENGTH_BYTES)};
    auto header_size = usize{COLUMN_COUNT_BYTES + bitmap_size.value + entries_size.value};

    std::vector<std::byte> encoded{};
    encoded.reserve(header_size.value);
    append_u32_le(encoded, static_cast<std::uint32_t>(column_count.value));
    encoded.resize(encoded.size() + bitmap_size.value, std::byte{0});

    std::vector<std::byte> entries{};
    entries.reserve(entries_size.value);
    std::vector<std::byte> payload{};

    for (usize index{0}; index < column_count; ++index) {
        const auto& column = schema[index.value];
        const auto& value = values[index.value];
        auto type = lookup_type(column.type_oid);
        if (!type) {
            return std::unexpected(type.error());
        }
        if (value.type_oid != column.type_oid) {
            return std::unexpected(EdbError::InvalidArgument);
        }

        if (value.is_null.value) {
            if (!column.nullable.value) {
                return std::unexpected(EdbError::InvalidArgument);
            }
            set_null_bit(
                std::span<std::byte>{encoded}.subspan(COLUMN_COUNT_BYTES, bitmap_size.value),
                index);
            append_u32_le(entries, 0U);
            append_u32_le(entries, 0U);
            continue;
        }

        if ((*type)->fixed_size.has_value() && value.bytes.size() != (*type)->fixed_size->value) {
            return std::unexpected(EdbError::InvalidArgument);
        }

        const auto offset = header_size + usize{payload.size()};
        append_u32_le(entries, static_cast<std::uint32_t>(offset.value));
        append_u32_le(entries, static_cast<std::uint32_t>(value.bytes.size()));
        payload.insert(payload.end(), value.bytes.begin(), value.bytes.end());
    }

    encoded.insert(encoded.end(), entries.begin(), entries.end());
    encoded.insert(encoded.end(), payload.begin(), payload.end());
    return encoded;
}

auto EdbRowCodec::decode(std::span<const std::byte> tuple) const
    -> EdbResult<std::vector<EdbValue>> {
    if (registry == nullptr) {
        return std::unexpected(EdbError::InvalidArgument);
    }

    const auto stored_columns = read_u32_le(tuple, usize{0});
    if (!stored_columns) {
        return std::unexpected(stored_columns.error());
    }
    if (*stored_columns != schema.size()) {
        return std::unexpected(EdbError::Corruption);
    }

    const auto column_count = usize{schema.size()};
    const auto bitmap_size = bitmap_bytes_for_columns(column_count);
    const auto entries_offset = usize{COLUMN_COUNT_BYTES + bitmap_size.value};
    const auto payload_offset =
        usize{entries_offset.value + (column_count.value * (OFFSET_BYTES + LENGTH_BYTES))};
    if (tuple.size() < payload_offset.value) {
        return std::unexpected(EdbError::Corruption);
    }

    const auto bitmap = tuple.subspan(COLUMN_COUNT_BYTES, bitmap_size.value);
    std::vector<EdbValue> values{};
    values.reserve(column_count.value);

    for (usize index{0}; index < column_count; ++index) {
        const auto& column = schema[index.value];
        auto type = lookup_type(column.type_oid);
        if (!type) {
            return std::unexpected(type.error());
        }

        const auto entry_offset =
            usize{entries_offset.value + (index.value * (OFFSET_BYTES + LENGTH_BYTES))};
        const auto offset = read_u32_le(tuple, entry_offset);
        const auto length = read_u32_le(tuple, usize{entry_offset.value + OFFSET_BYTES});
        if (!offset || !length) {
            return std::unexpected(EdbError::Corruption);
        }

        EdbValue value{.type_oid = column.type_oid, .bytes = {}, .is_null = b8{false}};
        if (is_null_bit_set(bitmap, index).value) {
            value.is_null = b8{true};
            values.push_back(std::move(value));
            continue;
        }

        const auto start = static_cast<std::size_t>(*offset);
        const auto count = static_cast<std::size_t>(*length);
        if (start < payload_offset.value || (start + count) > tuple.size()) {
            return std::unexpected(EdbError::Corruption);
        }
        if ((*type)->fixed_size.has_value() && count != (*type)->fixed_size->value) {
            return std::unexpected(EdbError::Corruption);
        }

        auto bytes = tuple.subspan(start, count);
        value.bytes.assign(bytes.begin(), bytes.end());
        values.push_back(std::move(value));
    }

    return values;
}

auto EdbRowCodec::columns() const -> std::span<const EdbColumnSchema> {
    return schema;
}

auto EdbRowCodec::lookup_type(u32 oid) const -> EdbResult<const EdbType*> {
    auto found = registry->lookup(oid);
    if (!found) {
        return std::unexpected(EdbError::TypeNotFound);
    }
    return *found;
}

}  // namespace edb