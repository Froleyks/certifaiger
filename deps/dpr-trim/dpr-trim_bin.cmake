include(ExternalProject)
set(DPR_TRIM_GIT_TAG "master" CACHE STRING "DPR-trim git commit hash or tag to checkout")

if(STATIC)
  set(DPR_TRIM_BUILD_COMMAND ${CMAKE_C_COMPILER} -O2 -static -o dpr-trim dpr-trim.c)
else()
  set(DPR_TRIM_BUILD_COMMAND ${CMAKE_C_COMPILER} -O2 -o dpr-trim dpr-trim.c)
endif()
set(DPR_TRIM_DIR "${CMAKE_SOURCE_DIR}/../dpr-trim" CACHE PATH "Path to a local dpr-trim checkout")
cmake_path(NORMAL_PATH DPR_TRIM_DIR)
if (EXISTS "${DPR_TRIM_DIR}/dpr-trim.c")
  message(STATUS "Using local dpr-trim at: ${DPR_TRIM_DIR}")
  set(_dpr_trim_source_args SOURCE_DIR "${DPR_TRIM_DIR}")
else()
  message(STATUS "Fetching dpr-trim from Git")
  set(_dpr_trim_source_args
    GIT_REPOSITORY https://github.com/marijnheule/dpr-trim.git
    GIT_TAG ${DPR_TRIM_GIT_TAG}
  )
endif()

ExternalProject_Add(dpr-trim
  ${_dpr_trim_source_args}
  PATCH_COMMAND  git apply ${CMAKE_CURRENT_LIST_DIR}/dpr-trim.patch
  CONFIGURE_COMMAND ""
  BUILD_COMMAND  ${DPR_TRIM_BUILD_COMMAND}
  BUILD_IN_SOURCE 1
  UPDATE_COMMAND ""
  INSTALL_COMMAND cp dpr-trim ${CMAKE_CURRENT_BINARY_DIR}
)
install(PROGRAMS ${CMAKE_CURRENT_BINARY_DIR}/dpr-trim TYPE BIN)
