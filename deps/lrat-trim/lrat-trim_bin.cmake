include(ExternalProject)
set(LRAT_TRIM_GIT_TAG "development" CACHE STRING "LRAT-trim git commit hash or tag to checkout")

if(STATIC)
  set(LRAT_TRIM_CONFIGURE_COMMAND ./configure -static)
else()
  set(LRAT_TRIM_CONFIGURE_COMMAND ./configure)
endif()

ExternalProject_Add(lrat-trim
  GIT_REPOSITORY https://github.com/arminbiere/lrat-trim.git
  GIT_TAG        ${LRAT_TRIM_GIT_TAG}
  CONFIGURE_COMMAND ${LRAT_TRIM_CONFIGURE_COMMAND}
  BUILD_COMMAND  make -j
  BUILD_IN_SOURCE 1
  UPDATE_COMMAND
  INSTALL_COMMAND cp lrat-trim ${CMAKE_CURRENT_BINARY_DIR}
)
install(PROGRAMS ${CMAKE_CURRENT_BINARY_DIR}/lrat-trim TYPE BIN)
