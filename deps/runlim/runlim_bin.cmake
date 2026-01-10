include(ExternalProject)
set(RUNLIM_GIT_TAG "master" CACHE STRING "runlim git commit hash or tag to checkout")

if(STATIC)
  set(RUNLIM_CONFIGURE_COMMAND
    ${CMAKE_COMMAND} -E env "CC=${CMAKE_C_COMPILER} -static" ./configure.sh
  )
else()
  set(RUNLIM_CONFIGURE_COMMAND
    ${CMAKE_COMMAND} -E env CC=${CMAKE_C_COMPILER} ./configure.sh
  )
endif()

set(RUNLIM_DIR "${CMAKE_SOURCE_DIR}/../runlim" CACHE PATH "Path to a local runlim checkout")
cmake_path(NORMAL_PATH RUNLIM_DIR)
if (EXISTS "${RUNLIM_DIR}/configure.sh")
  message(STATUS "Using local runlim at: ${RUNLIM_DIR}")
  set(_runlim_source_args SOURCE_DIR "${RUNLIM_DIR}")
else()
  message(STATUS "Fetching runlim from Git")
  set(_runlim_source_args
    GIT_REPOSITORY https://github.com/arminbiere/runlim.git
    GIT_TAG ${RUNLIM_GIT_TAG}
  )
endif()

ExternalProject_Add(runlim_bin
  ${_runlim_source_args}
  BUILD_IN_SOURCE 1
  UPDATE_COMMAND ""
  CONFIGURE_COMMAND ${RUNLIM_CONFIGURE_COMMAND}
  BUILD_COMMAND make -j runlim
  INSTALL_COMMAND cp runlim ${CMAKE_CURRENT_BINARY_DIR}
)
install(PROGRAMS ${CMAKE_CURRENT_BINARY_DIR}/runlim TYPE BIN)
