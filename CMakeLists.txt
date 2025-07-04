cmake_minimum_required(VERSION 3.10)
project(certifaiger VERSION 7.1.3)
set(CMAKE_CXX_STANDARD 23)
file(GLOB headers CONFIGURE_DEPENDS src/*.hpp)
file(GLOB sources CONFIGURE_DEPENDS src/*.cpp)

option(ASAN "Build with address sanitizer" OFF)
option(LTO "Enable Link-Time Optimization" ON)
option(STATIC "Build static binary" OFF)
option(SAT "Build SAT solver (kissat)" ON)
option(CHECK "Setup checking and fuzzing" ON)
option(QBF "Build QBF solver (non-stratified only)" OFF)

include(util.cmake)
include(deps/aiger/aiger_lib.cmake)
if(SAT)
  include(deps/kissat/kissat_bin.cmake)
endif()
if(CHECK)
  include(deps/aiger/aiger_bin.cmake)
  add_scripts(
    check
    check_unsat
    check_sat
    certified
    random
    fuzz
    status
    certifuzzer)
endif()
if(QBF)
  include(deps/quabs/quabs_bin.cmake)
endif()

add_executable(certifaiger ${sources})
target_compile_options(certifaiger PRIVATE -DGITID="${GIT_ID}")
target_compile_options(certifaiger PRIVATE -DVERSION="${PROJECT_VERSION}")
install(TARGETS certifaiger)
target_link_libraries(certifaiger aiger)

if(STATIC)
  set(BUILD_SHARED_LIBS OFF)
  set_target_properties(certifaiger PROPERTIES LINK_FLAGS "-static")
endif()
