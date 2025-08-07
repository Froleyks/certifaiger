cmake_minimum_required(VERSION 3.19)
project(certifaiger VERSION 8.6.0)
set(CMAKE_CXX_STANDARD 23)
file(GLOB headers CONFIGURE_DEPENDS src/*.hpp)
file(GLOB sources CONFIGURE_DEPENDS src/*.cpp)

option(ASAN "Build with address sanitizer" OFF)
option(LTO "Enable Link-Time Optimization" ON)
option(STATIC "Build static binary" OFF)
option(CHECK "Setup checking and fuzzing" ON)
option(PROOF "Check SAT solver proofs" OFF)
set(SAT "kissat" CACHE STRING "SAT solver to use: kissat or cadical")
set_property(CACHE SAT PROPERTY STRINGS kissat cadical)

include(util.cmake)
include(deps/aiger/aiger_lib.cmake)

set(SAT_CHECKER "")
if(SAT STREQUAL "kissat")
  include(deps/kissat/kissat_bin.cmake)
  if(PROOF)
    include(deps/dpr-trim/dpr-trim_bin.cmake)
    set(SAT_CHECKER "dpr-trim")
  endif()
elseif(SAT STREQUAL "cadical")
  include(deps/cadical/cadical_bin.cmake)
  if(PROOF)
    include(deps/lrat-trim/lrat-trim_bin.cmake)
    set(SAT_CHECKER "lrat-trim")
  endif()
else()
  message(WARNING "Not building SAT solver ${SAT}, place binary in bin directory")
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

add_executable(certifaiger ${sources})
target_compile_options(certifaiger PRIVATE -DGITID="${GIT_ID}")
target_compile_options(certifaiger PRIVATE -DVERSION="${PROJECT_VERSION}")
install(TARGETS certifaiger)
target_link_libraries(certifaiger aiger)

if(STATIC)
  set(BUILD_SHARED_LIBS OFF)
  set_target_properties(certifaiger PROPERTIES LINK_FLAGS "-static")
endif()
