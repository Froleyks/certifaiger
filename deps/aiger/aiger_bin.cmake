set(AIGER_TOOLS aigtocnf aigsim aigfuzz aigdd aigtoaig aigsplit aigfuzz)

ExternalProject_Add(
  aiger_tools
  GIT_REPOSITORY https://github.com/arminbiere/aiger
  BUILD_IN_SOURCE 1
  UPDATE_COMMAND ""
  CONFIGURE_COMMAND ./configure.sh -static
  BUILD_COMMAND make -j ${AIGER_TOOLS}
  INSTALL_COMMAND cp ${AIGER_TOOLS} ${CMAKE_CURRENT_BINARY_DIR} && make clean)
foreach(tool IN LISTS AIGER_TOOLS)
  install(PROGRAMS ${CMAKE_CURRENT_BINARY_DIR}/${tool} TYPE BIN)
endforeach()
