# Phase 3 — Type System 🔲

Extensible type registry modelled after PostgreSQL's `pg_type`. Types can be built-in or loaded at runtime from extensions.

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

## Deliverables

- [ ] `EdbTypeImpl` concept + `EdbType` struct
- [ ] `EdbTypeRegistry` with OID assignment
- [ ] All 5 built-in types with unit tests (from_text/to_text round-trip, compare ordering, hash consistency)
