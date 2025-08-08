include(ExternalProject)
set(DPR_TRIM_GIT_TAG "master" CACHE STRING "DPR-trim git commit hash or tag to checkout")

if(STATIC)
  set(DPR_TRIM_BUILD_COMMAND ${CMAKE_C_COMPILER} -O2 -static -o dpr-trim dpr-trim.c)
else()
  set(DPR_TRIM_BUILD_COMMAND ${CMAKE_C_COMPILER} -O2 -o dpr-trim dpr-trim.c)
endif()

ExternalProject_Add(dpr-trim
  GIT_REPOSITORY https://github.com/marijnheule/dpr-trim.git
  GIT_TAG        ${DPR_TRIM_GIT_TAG}
  PATCH_COMMAND  git apply ${CMAKE_CURRENT_LIST_DIR}/dpr-trim.patch
  CONFIGURE_COMMAND ""
  BUILD_COMMAND  ${DPR_TRIM_BUILD_COMMAND}
  BUILD_IN_SOURCE 1
  UPDATE_COMMAND ""
  INSTALL_COMMAND cp dpr-trim ${CMAKE_CURRENT_BINARY_DIR}
)
install(PROGRAMS ${CMAKE_CURRENT_BINARY_DIR}/dpr-trim TYPE BIN)
