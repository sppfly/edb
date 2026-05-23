# src/storage/io/posix — POSIX file I/O backend

add_library(edb_io_posix STATIC
    posix_io.cpp
)

target_link_libraries(edb_io_posix
    PUBLIC edb_io
)
