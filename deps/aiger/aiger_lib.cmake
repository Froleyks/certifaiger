include(FetchContent)
set(_aiger_fetch_args
  GIT_REPOSITORY https://github.com/arminbiere/aiger
)

set(AIGER_DIR "${CMAKE_SOURCE_DIR}/../aiger" CACHE PATH "Path to a local aiger checkout")
cmake_path(NORMAL_PATH AIGER_DIR)
if (EXISTS "${AIGER_DIR}/configure.sh")
  message(STATUS "Using local aiger at: ${AIGER_DIR}")
  set(_aiger_fetch_args SOURCE_DIR "${AIGER_DIR}")
else()
  message(STATUS "Fetching aiger from Git")
endif()

FetchContent_Declare(aiger
  ${_aiger_fetch_args}
  PATCH_COMMAND cp ${CMAKE_CURRENT_LIST_DIR}/CMakeLists.txt .
)
FetchContent_MakeAvailable(aiger)
