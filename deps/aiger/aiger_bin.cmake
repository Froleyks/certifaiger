include(ExternalProject)
set(AIGER_TOOLS aigtocnf aigsim aigfuzz aigdd aigtoaig aigor aigsplit aigfuzz)

set(AIGER_DIR "${CMAKE_SOURCE_DIR}/../aiger" CACHE PATH "Path to a local aiger checkout")
cmake_path(NORMAL_PATH AIGER_DIR)
if (EXISTS "${AIGER_DIR}/configure.sh")
  message(STATUS "Using local aiger at: ${AIGER_DIR}")
  set(_aiger_source_args SOURCE_DIR "${AIGER_DIR}")
else()
  message(STATUS "Fetching aiger from Git")
  set(_aiger_source_args
    GIT_REPOSITORY https://github.com/arminbiere/aiger
    GIT_TAG development
  )
endif()

ExternalProject_Add(
  aiger_tools
  ${_aiger_source_args}
  BUILD_IN_SOURCE 1
  UPDATE_COMMAND ""
  CONFIGURE_COMMAND ./configure.sh -static
  BUILD_COMMAND make -j ${AIGER_TOOLS}
  INSTALL_COMMAND cp ${AIGER_TOOLS} ${CMAKE_CURRENT_BINARY_DIR} && make clean)
foreach(tool IN LISTS AIGER_TOOLS)
  install(PROGRAMS ${CMAKE_CURRENT_BINARY_DIR}/${tool} TYPE BIN)
endforeach()
