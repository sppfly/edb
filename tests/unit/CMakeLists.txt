include(FetchContent)

FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG        v1.15.2
)
# Prevent GoogleTest from overriding our compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# gtest 1.15.2 trips clang 22's -Wcharacter-conversion under our -Werror
foreach(target gtest gtest_main gmock gmock_main)
  target_compile_options(${target} PRIVATE -Wno-character-conversion)
endforeach()

add_subdirectory(utils)
add_subdirectory(storage)
add_subdirectory(types)
add_subdirectory(catalog)
add_subdirectory(lock)
add_subdirectory(query)
add_subdirectory(transaction)
add_subdirectory(wal)
