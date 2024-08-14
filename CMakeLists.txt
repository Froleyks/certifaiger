cmake_minimum_required(VERSION 3.16.3)
project(certifaiger)

file(GLOB headers CONFIGURE_DEPENDS src/*.hpp)
file(GLOB sources CONFIGURE_DEPENDS src/*.cpp)

# Options:
option(LTO "Enable Link-Time Optimization" ON)
option(GIT "Automatically download dependencies" ON)
option(CHECK "Setup checking and fuzzing" ON)
option(SAT "Build SAT solver (kissat)" ON)
option(QBF "Build QBF solver (non-stratified only)" OFF)

set(AIGER_DIR ${CMAKE_CURRENT_LIST_DIR}/dependencies/aiger CACHE PATH "aiger directory")
set(KISSAT_DIR ${CMAKE_CURRENT_LIST_DIR}/dependencies/kissat CACHE PATH
                                                                   "kissat directory")
set(QUABS_DIR ${CMAKE_CURRENT_LIST_DIR}/dependencies/quabs CACHE PATH "quabs directory")

# Compile flags:
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(LTO)
  set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
endif(LTO)
set(CMAKE_CXX_FLAGS_FUZZING "-O3" CACHE STRING "Flags for fuzzing build type" FORCE)

# Dependencies:
include(ExternalProject)
function(git_submodule)
  if(GIT AND NOT EXISTS ${CMAKE_CURRENT_LIST_DIR}/dependencies/${ARGV0})
    find_package(Git QUIET)
    execute_process(
      COMMAND ${GIT_EXECUTABLE} submodule update --init --recursive ${ARGV0}
      WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/dependencies RESULT_VARIABLE GIT_RES)
    if(NOT GIT_RES EQUAL "0")
      message(
        FATAL_ERROR
          "git submodule update --init --recursive failed with ${GIT_RES}, please checkout submodules"
      )
    endif()
  endif()
endfunction()

function(add_script)
  message(STATUS ${ARGV0})
  set(SCRIPT_DIR ${CMAKE_CURRENT_LIST_DIR}/scripts)
  configure_file(
    ${SCRIPT_DIR}/${ARGV0}
    ${CMAKE_CURRENT_BINARY_DIR}/${ARGV1}
    COPYONLY
    FILE_PERMISSIONS
    OWNER_READ
    OWNER_WRITE
    OWNER_EXECUTE
    GROUP_READ
    GROUP_WRITE
    GROUP_EXECUTE
    WORLD_READ
    WORLD_EXECUTE)
endfunction()

if(NOT TARGET aiger)
  git_submodule(aiger)
  find_file(AIGER_SRC NAMES aiger.c PATHS ${AIGER_DIR})
  if(NOT AIGER_SRC)
    message(FATAL_ERROR "Aiger library not found in ${AIGER_DIR}.")
  endif()
  add_library(aiger STATIC ${AIGER_DIR}/aiger.c)
  target_include_directories(aiger PUBLIC ${AIGER_DIR}/)
endif()

if(CHECK)
  git_submodule(aiger)
  ExternalProject_Add(
    aigtools
    PREFIX aiger
    SOURCE_DIR ${AIGER_DIR}
    BUILD_IN_SOURCE 1
    CONFIGURE_COMMAND ./configure.sh
    BUILD_COMMAND make -j aigtocnf aigsim aigfuzz aigdd aigtoaig
    INSTALL_COMMAND cp aigtocnf aigsim aigfuzz aigdd aigtoaig <INSTALL_DIR> && make clean)
  add_script(check.in check)
  add_script(simulate.in simulate)
  add_script(certified.in certified)
  add_script(random.in random)
  add_script(fuzz.in fuzz)
  add_script(status.in status)
  add_script(certifuzzer.in certifuzzer)
endif()

if(SAT)
  git_submodule(kissat)
  ExternalProject_Add(
    kissat
    PREFIX kissat
    SOURCE_DIR ${KISSAT_DIR}
    BUILD_IN_SOURCE 1
    CONFIGURE_COMMAND ./configure
    BUILD_COMMAND make -j
    INSTALL_COMMAND cp build/kissat <INSTALL_DIR> && make clean)
endif()

if(QBF)
  git_submodule(quabs)
  # The CMS version they use has a deprecated python interface dependency
  set(patch_command
      bash -c
      # The patch command will return 1 if the patch is already applied
      "(patch --forward libsolve/src/CMakeLists.txt ${CMAKE_CURRENT_LIST_DIR}/dependencies/quabs.patch || [ $? -eq 1 ] && exit 0)"
  )
  ExternalProject_Add(
    quabs
    PREFIX quabs
    SOURCE_DIR ${QUABS_DIR}
    PATCH_COMMAND ${patch_command}
    INSTALL_COMMAND cp quabs qaiger2qcir <INSTALL_DIR>)
endif()

# Targets:
# # Format:
add_custom_target(
  format_${PROJECT_NAME}
  COMMAND clang-format -i ${sources} ${headers} || echo "clang-format not installed"
  COMMAND cmake-format -i ${CMAKE_CURRENT_LIST_FILE} || echo "cmake-format not installed"
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  COMMENT "Formatting files")
set_target_properties(format_${PROJECT_NAME} PROPERTIES EXCLUDE_FROM_ALL TRUE)

# # Certifaiger:
file(READ "${CMAKE_CURRENT_LIST_DIR}/VERSION" VERSION)
string(STRIP ${VERSION} VERSION)
execute_process(COMMAND bash "-c" "git show|head -1|awk '{print $2}'"
                OUTPUT_STRIP_TRAILING_WHITESPACE OUTPUT_VARIABLE GIT_ID)
add_definitions("-DVERSION=\"${VERSION}\"")
add_definitions("-DGITID=\"${GIT_ID}\"")

add_executable(certifaiger ${sources})
target_link_libraries(certifaiger aiger)
install(TARGETS certifaiger)
