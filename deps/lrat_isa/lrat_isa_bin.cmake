include(ExternalProject)
set(LRAT_ISA_GIT_TAG "main" CACHE STRING "lrat_isa git commit hash or tag to checkout")

if(STATIC)
  set(LRAT_ISA_BUILD_COMMAND make)
else()
  set(LRAT_ISA_BUILD_COMMAND make)
endif()
set(LRAT_ISA_DIR "${CMAKE_SOURCE_DIR}/../lrat_isa" CACHE PATH "Path to a local lrat_isa checkout")
cmake_path(NORMAL_PATH LRAT_ISA_DIR)
if (EXISTS "${LRAT_ISA_DIR}/Makefile")
  message(STATUS "Using local lrat_isa at: ${LRAT_ISA_DIR}")
  set(_lrat_isa_source_args SOURCE_DIR "${LRAT_ISA_DIR}")
else()
  message(STATUS "Fetching lrat_isa from Git")
  set(_lrat_isa_source_args
    GIT_REPOSITORY https://github.com/lammich/lrat_isa.git
    GIT_TAG ${LRAT_ISA_GIT_TAG}
  )
endif()

ExternalProject_Add(lrat_isa
  ${_lrat_isa_source_args}
  CONFIGURE_COMMAND ""
  BUILD_COMMAND  ${LRAT_ISA_BUILD_COMMAND}
  BUILD_IN_SOURCE 1
  UPDATE_COMMAND ""
  INSTALL_COMMAND cp code/lrat_isa ${CMAKE_CURRENT_BINARY_DIR}
)
install(PROGRAMS ${CMAKE_CURRENT_BINARY_DIR}/lrat_isa TYPE BIN)
