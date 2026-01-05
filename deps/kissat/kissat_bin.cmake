include(ExternalProject)
if(STATIC)
  set(KISSAT_CONFIGURE_COMMAND ./configure -static)
else()
  set(KISSAT_CONFIGURE_COMMAND ./configure)
endif()
set(KISSAT_DIR "${CMAKE_SOURCE_DIR}/../kissat" CACHE PATH "Path to a local kissat checkout")
cmake_path(NORMAL_PATH KISSAT_DIR)
if (EXISTS "${KISSAT_DIR}/configure")
  message(STATUS "Using local kissat at: ${KISSAT_DIR}")
  set(_kissat_source_args SOURCE_DIR "${KISSAT_DIR}")
else()
  message(STATUS "Fetching kissat from Git")
  set(_kissat_source_args
    GIT_REPOSITORY https://github.com/arminbiere/kissat.git
    GIT_TAG master
  )
endif()
ExternalProject_Add(
  kissat
  ${_kissat_source_args}
  BUILD_IN_SOURCE 1
  UPDATE_COMMAND ""
  CONFIGURE_COMMAND ${KISSAT_CONFIGURE_COMMAND}
  BUILD_COMMAND make -j
  INSTALL_COMMAND cp build/kissat ${CMAKE_CURRENT_BINARY_DIR})
install(PROGRAMS ${CMAKE_CURRENT_BINARY_DIR}/kissat TYPE BIN)
