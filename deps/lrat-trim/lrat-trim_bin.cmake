include(ExternalProject)
set(LRAT_TRIM_GIT_TAG "development" CACHE STRING "LRAT-trim git commit hash or tag to checkout")

if(STATIC)
  set(LRAT_TRIM_BUILD_COMMAND ${CMAKE_C_COMPILER} -O2 -static -o lrat-trim lrat-trim.c)
else()
  set(LRAT_TRIM_BUILD_COMMAND ${CMAKE_C_COMPILER} -O2 -o lrat-trim lrat-trim.c)
endif()
set(LRAT_TRIM_DIR "${CMAKE_SOURCE_DIR}/../lrat-trim" CACHE PATH "Path to a local lrat-trim checkout")
cmake_path(NORMAL_PATH LRAT_TRIM_DIR)
if (EXISTS "${LRAT_TRIM_DIR}/lrat-trim.c")
  message(STATUS "Using local lrat-trim at: ${LRAT_TRIM_DIR}")
  set(_lrat_trim_source_args SOURCE_DIR "${LRAT_TRIM_DIR}")
else()
  message(STATUS "Fetching lrat-trim from Git")
  set(_lrat_trim_source_args
    GIT_REPOSITORY https://github.com/arminbiere/lrat-trim.git
    GIT_TAG ${LRAT_TRIM_GIT_TAG}
  )
endif()

ExternalProject_Add(lrat-trim
  ${_lrat_trim_source_args}
  CONFIGURE_COMMAND ""
  BUILD_COMMAND  ${LRAT_TRIM_BUILD_COMMAND}
  BUILD_IN_SOURCE 1
  UPDATE_COMMAND ""
  INSTALL_COMMAND cp lrat-trim ${CMAKE_CURRENT_BINARY_DIR}
)
install(PROGRAMS ${CMAKE_CURRENT_BINARY_DIR}/lrat-trim TYPE BIN)
