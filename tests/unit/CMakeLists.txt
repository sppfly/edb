# Vendored googletest v1.15.2 (BSD-3-Clause) in third_party/googletest.
# To update: replace that directory with the new release.
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../../third_party/googletest"
                 "${CMAKE_CURRENT_BINARY_DIR}/googletest")

# gtest 1.15.2 trips clang 22's -Wcharacter-conversion under our -Werror
foreach(target gtest gtest_main gmock gmock_main)
  target_compile_options(${target} PRIVATE -Wno-character-conversion)
endforeach()

add_subdirectory(utils)
add_subdirectory(storage)
add_subdirectory(types)
add_subdirectory(catalog)
add_subdirectory(query)
