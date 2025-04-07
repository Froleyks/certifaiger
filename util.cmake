set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_INSTALL_MESSAGE NEVER)
if(LTO)
  set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
endif(LTO)
if(ASAN)
  add_compile_options(-fsanitize=address)
  add_link_options(-fsanitize=address)
endif()

execute_process(
  COMMAND sh "-c" "git rev-parse --revs-only HEAD --"
  OUTPUT_STRIP_TRAILING_WHITESPACE
  OUTPUT_VARIABLE GIT_ID
  ERROR_QUIET)

function(add_scripts)
  foreach(file IN LISTS ARGN)
    configure_file(
      ${CMAKE_CURRENT_LIST_DIR}/scripts/${file}.in
      ${CMAKE_CURRENT_BINARY_DIR}/${file}
      @ONLY
      FILE_PERMISSIONS
      OWNER_READ
      OWNER_WRITE
      OWNER_EXECUTE
      GROUP_READ
      GROUP_WRITE
      GROUP_EXECUTE
      WORLD_READ
      WORLD_EXECUTE)
    install(PROGRAMS ${CMAKE_CURRENT_BINARY_DIR}/${file} TYPE BIN)
  endforeach()
endfunction()

add_custom_target(
  format_${PROJECT_NAME}
  COMMAND clang-format -i ${sources} ${headers} || echo "clang-format not installed"
  COMMAND cmake-format -i ${CMAKE_CURRENT_LIST_FILE} || echo "cmake-format not installed"
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  COMMENT "Formatting files")
set_target_properties(format_${PROJECT_NAME} PROPERTIES EXCLUDE_FROM_ALL TRUE)
