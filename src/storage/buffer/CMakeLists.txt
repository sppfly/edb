# src/storage/buffer — Format-agnostic buffer pool

add_library(edb_buffer STATIC
    buffer_pool.cpp
)

target_link_libraries(edb_buffer
    PUBLIC edb_page
)
