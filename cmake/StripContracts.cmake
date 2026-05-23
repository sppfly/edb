if(NOT DEFINED INPUT)
  message(FATAL_ERROR "INPUT is required")
endif()

if(NOT DEFINED OUTPUT)
  message(FATAL_ERROR "OUTPUT is required")
endif()

file(READ "${INPUT}" EDB_COMPILE_COMMANDS)
string(REPLACE " -fcontracts" "" EDB_COMPILE_COMMANDS "${EDB_COMPILE_COMMANDS}")
get_filename_component(OUTPUT_DIR "${OUTPUT}" DIRECTORY)
file(MAKE_DIRECTORY "${OUTPUT_DIR}")
file(WRITE "${OUTPUT}" "${EDB_COMPILE_COMMANDS}")
