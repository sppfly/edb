# src/storage/engine — Pluggable storage engine interface

add_library(edb_engine STATIC
    engine_ops.cpp
)

target_include_directories(edb_engine
    PUBLIC ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(edb_engine
    PUBLIC edb_page
)
