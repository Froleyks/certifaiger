set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_INSTALL_MESSAGE NEVER)

execute_process(
  COMMAND sh "-c" "git rev-parse --revs-only HEAD --"
  OUTPUT_STRIP_TRAILING_WHITESPACE
  OUTPUT_VARIABLE GIT_ID
  ERROR_QUIET)

function(util_add_options target)
  if(STATIC)
    set(BUILD_SHARED_LIBS OFF)
    set_target_properties(${target} PROPERTIES LINK_FLAGS "-static")
  endif()

  if(LTO)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT _ipo_ok OUTPUT _ipo_msg)
    if(_ipo_ok)
      set_property(TARGET ${target} PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
    endif()
  endif()

  if(STRICT)
    set_property(TARGET ${target} PROPERTY CXX_EXTENSIONS OFF)
    target_compile_options(
      ${target}
      PRIVATE -Werror
              -Wall
              -Wextra
              -Wpedantic
              -Wshadow
              -Wconversion
              -Wsign-conversion
              -Wnull-dereference
              -Wformat=2
              -Wformat-security
              -Wduplicated-cond
              -Wduplicated-branches
              -Wlogical-op
              # aiger library does not comply
              # -Wuseless-cast
              # -Wold-style-cast
              -Wnon-virtual-dtor
              -Woverloaded-virtual
              -Wcast-align=strict
              -Wdouble-promotion
              -Wmissing-declarations
              $<$<COMPILE_LANGUAGE:C>:-Wmissing-prototypes>
              -Wundef
              -Werror=return-type
              -Werror=format-security)
  endif()

  if(ASSERT)
    target_compile_options(${target} PRIVATE -UNDEBUG)
    target_compile_definitions(
      ${target} PRIVATE _GLIBCXX_ASSERTIONS _LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_DEBUG)
  endif()

  if(ASAN)
    target_compile_options(${target} PRIVATE -fsanitize=address)
    target_link_options(${target} PRIVATE -fsanitize=address)
  endif()

  if(UBSAN)
    target_compile_options(${target} PRIVATE -fsanitize=undefined)
    target_link_options(${target} PRIVATE -fsanitize=undefined)
    target_compile_options(${target} PRIVATE -fno-omit-frame-pointer -fno-common)
  endif()

  if(ASAN OR UBSAN)
    target_compile_options(${target} PRIVATE -g)
  endif()
endfunction()

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
