# src/storage/page — Logical page store over EdbStorageIOOps

add_library(edb_page STATIC
    page_store.cpp
)

target_link_libraries(edb_page
    PUBLIC edb_io
)
