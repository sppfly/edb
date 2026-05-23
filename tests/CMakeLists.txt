add_subdirectory(unit)
add_subdirectory(regression)

if(EDB_BUILD_BENCH)
    add_subdirectory(storage)
endif()
