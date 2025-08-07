include(ExternalProject)
set(CADICAL_GIT_TAG "0f9eea8e21bb75de570e1cc2fcd1196bc74470ac" CACHE STRING "CaDiCaL git commit hash or tag to checkout")

if(STATIC)
  set(CADICAL_CONFIGURE_COMMAND ./configure -static)
else()
  set(CADICAL_CONFIGURE_COMMAND ./configure)
endif()

set(CADICAL_DIR ${CMAKE_BINARY_DIR}/cadical-prefix/src/cadical)
set(CADICAL_INCLUDE_DIR ${CADICAL_DIR}/src)
set(CADICAL_LIB ${CADICAL_DIR}/build/libcadical.a)

ExternalProject_Add(
  cadical
  GIT_REPOSITORY https://github.com/arminbiere/cadical
  GIT_TAG ${CADICAL_GIT_TAG}
  CONFIGURE_COMMAND ${CADICAL_CONFIGURE_COMMAND}
  BUILD_COMMAND make -j
  BUILD_IN_SOURCE 1
  UPDATE_COMMAND ""
  INSTALL_COMMAND cp build/cadical ${CMAKE_CURRENT_BINARY_DIR})
install(PROGRAMS ${CMAKE_CURRENT_BINARY_DIR}/cadical TYPE BIN)
