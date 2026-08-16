# src/storage/buffer — Format-agnostic buffer pool

add_library(edb_buffer STATIC
    arc_policy.cpp
    buffer_pool.cpp
    clock_sweep_policy.cpp
    eviction_policy.cpp
    lru_k_policy.cpp
)

target_link_libraries(edb_buffer
    PUBLIC edb_page
)
