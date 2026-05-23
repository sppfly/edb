# src/storage/io — Storage I/O backend interface

add_library(edb_io INTERFACE)

target_include_directories(edb_io
    INTERFACE ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(edb_io
    INTERFACE edb_utils
)

add_subdirectory(posix)
