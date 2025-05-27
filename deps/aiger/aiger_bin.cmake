set(AIGER_TOOLS aigtocnf aigsim aigfuzz aigdd aigtoaig aigfuzz)
set(patch "${CMAKE_CURRENT_LIST_DIR}/static.patch")
if(STATIC)
  set(AIGER_TOOLS_PATCH_COMMAND git apply ${patch})
else()
  set(AIGER_TOOLS_PATCH_COMMAND "")
endif()

ExternalProject_Add(
  aiger_tools
  GIT_REPOSITORY https://github.com/arminbiere/aiger
  GIT_TAG development
  BUILD_IN_SOURCE 1
  UPDATE_COMMAND ""
  CONFIGURE_COMMAND ./configure.sh -static
  BUILD_COMMAND make -j ${AIGER_TOOLS}
  INSTALL_COMMAND cp ${AIGER_TOOLS} ${CMAKE_CURRENT_BINARY_DIR} && make clean)
foreach(tool IN LISTS AIGER_TOOLS)
  install(PROGRAMS ${CMAKE_CURRENT_BINARY_DIR}/${tool} TYPE BIN)
endforeach()
