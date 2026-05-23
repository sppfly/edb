add_executable(io_bench io_bench.cpp)

target_link_libraries(io_bench PRIVATE edb_io_posix)
