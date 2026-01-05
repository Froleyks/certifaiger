include(ExternalProject)
set(CADICAL_GIT_TAG "master" CACHE STRING "CaDiCaL git commit hash or tag to checkout")

if(STATIC)
  set(CADICAL_CONFIGURE_COMMAND ./configure -static)
else()
  set(CADICAL_CONFIGURE_COMMAND ./configure)
endif()

set(CADICAL_DIR "${CMAKE_SOURCE_DIR}/../cadical" CACHE PATH "Path to a local cadical checkout")
cmake_path(NORMAL_PATH CADICAL_DIR)
if (EXISTS "${CADICAL_DIR}/configure")
  message(STATUS "Using local cadical at: ${CADICAL_DIR}")
  set(_cadical_source_args SOURCE_DIR "${CADICAL_DIR}")
else()
  message(STATUS "Fetching cadical from Git")
  set(_cadical_source_args
    GIT_REPOSITORY https://github.com/arminbiere/cadical
    GIT_TAG ${CADICAL_GIT_TAG}
  )
endif()

set(CADICAL_DIR ${CMAKE_BINARY_DIR}/cadical-prefix/src/cadical)
set(CADICAL_INCLUDE_DIR ${CADICAL_DIR}/src)
set(CADICAL_LIB ${CADICAL_DIR}/build/libcadical.a)

ExternalProject_Add(
  cadical
  ${_cadical_source_args}
  CONFIGURE_COMMAND ${CADICAL_CONFIGURE_COMMAND}
  BUILD_COMMAND make -j
  BUILD_IN_SOURCE 1
  UPDATE_COMMAND ""
  INSTALL_COMMAND cp build/cadical ${CMAKE_CURRENT_BINARY_DIR})
install(PROGRAMS ${CMAKE_CURRENT_BINARY_DIR}/cadical TYPE BIN)
