add_library(aiger STATIC "${aiger_SOURCE_DIR}/aiger.c")
# Only expose the public header. On case-insensitive filesystems, the source
# directory's VERSION file would otherwise shadow the C++ <version> header.
configure_file("${aiger_SOURCE_DIR}/aiger.h" include/aiger.h COPYONLY)
target_include_directories(aiger PUBLIC "${CMAKE_CURRENT_BINARY_DIR}/include")
set_target_properties(aiger PROPERTIES PUBLIC_HEADER "${aiger_SOURCE_DIR}/aiger.h")
