set(patch "${CMAKE_CURRENT_LIST_DIR}/quabs.patch")
include(ExternalProject)
ExternalProject_Add(
  quabs
  GIT_REPOSITORY https://github.com/ltentrup/quabs.git
  GIT_TAG master
  UPDATE_COMMAND ""
  PATCH_COMMAND git apply --reverse ${patch} || true
  COMMAND git apply ${patch}
  INSTALL_COMMAND cp quabs qaiger2qcir ${CMAKE_CURRENT_BINARY_DIR})
install(PROGRAMS ${CMAKE_CURRENT_BINARY_DIR}/quabs TYPE BIN)
install(PROGRAMS ${CMAKE_CURRENT_BINARY_DIR}/qaiger2qcir TYPE BIN)
