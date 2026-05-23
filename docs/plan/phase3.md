# Phase 3 — Type System 🔲

Extensible type registry modelled after PostgreSQL's `pg_type`. Types can be built-in or loaded at runtime from extensions.

Phase 3 also provides the typed value and row-encoding primitives needed by Phase 4a so developers can construct a table-like object in code, insert typed rows, scan heap tuples, and decode them back into values before the SQL layer exists.

## Interface: `EdbTypeOps` (`src/types/type_ops.hpp`)

```cpp
/// Concept every concrete type must satisfy.
template <typename T>
concept EdbTypeImpl = requires(T v, std::string_view text,
                                const T& a, const T& b) {
    { T::from_text(text) } -> std::same_as<EdbResult<T>>;
    { T::to_text(v)      } -> std::same_as<std::string>;
    { T::compare(a, b)   } -> std::same_as<std::strong_ordering>;
    { T::hash(v)         } -> std::same_as<usize>;
    { T::fixed_size()    } -> std::same_as<std::optional<usize>>;
};

struct EdbType {
    u32          oid;
    std::string  name;
    std::optional<usize> fixed_size;   // nullopt → variable-length

    std::function<EdbResult<std::vector<std::byte>>(std::string_view)> from_text;
    std::function<std::string(std::span<const std::byte>)>             to_text;
    std::function<std::strong_ordering(std::span<const std::byte>,
                                       std::span<const std::byte>)>    compare;
    std::function<usize(std::span<const std::byte>)>                   hash;
};
```

## Registry (`src/types/registry.hpp`)

```cpp
class EdbTypeRegistry {
public:
    template <EdbTypeImpl T>
    auto register_type(std::string_view name) -> EdbResult<void>;

    auto lookup(std::string_view name) const -> EdbResult<const EdbType*>;
    auto lookup(u32 oid)               const -> EdbResult<const EdbType*>;
};
```

## Built-in Types

| Name | C++ type | Fixed size |
|---|---|---|
| `int32` | `i32` | 4 |
| `int64` | `i64` | 8 |
| `float64` | `f64` | 8 |
| `bool` | `b8` | 1 |
| `text` | `std::string` | variable |

## Typed Values and Row Encoding

Phase 2 stores raw tuple bytes. Phase 3 defines how typed values become those bytes.

```cpp
struct EdbValue {
    u32 type_oid;
    std::vector<std::byte> bytes;
};

struct EdbColumnSchema {
    std::string name;
    u32 type_oid;
    b8 nullable;
};

class EdbRowCodec {
public:
    auto encode(std::span<const EdbValue> values) const -> EdbResult<std::vector<std::byte>>;
    auto decode(std::span<const std::byte> tuple) const -> EdbResult<std::vector<EdbValue>>;
};
```

The row codec is schema-driven but catalog-independent: Phase 4a can build an `EdbTable` from an in-memory schema first, then Phase 4b/4c can persist and bootstrap those schemas through system catalog tables.

## Sub-phases

### Phase 3a — Type Registry

- Define `EdbTypeImpl`, `EdbType`, and `EdbTypeRegistry`
- Assign stable OIDs for built-in types
- Support lookup by name and OID

### Phase 3b — Built-in Types

- Implement `int32`, `int64`, `float64`, `bool`, and `text`
- Test parse/format round-trip, compare ordering, and hash consistency

### Phase 3c — Typed Values and Row Encoding

- Define `EdbValue`, `EdbColumnSchema`, and `EdbRowCodec`
- Encode fixed-width and variable-width values into heap tuple bytes
- Decode heap tuple bytes back into typed values
- Unit test row round-trips for mixed fixed/variable schemas

## Deliverables

- [ ] `EdbTypeImpl` concept + `EdbType` struct
- [ ] `EdbTypeRegistry` with OID assignment
- [ ] All 5 built-in types with unit tests (from_text/to_text round-trip, compare ordering, hash consistency)
- [ ] `EdbValue` and schema-driven `EdbRowCodec`
- [ ] Unit tests for encode/decode round-trips over mixed schemas
