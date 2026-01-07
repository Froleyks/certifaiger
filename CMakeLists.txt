cmake_minimum_required(VERSION 3.19)
include(util.cmake)
project(certifaiger VERSION 9.0.1)

set(CMAKE_CXX_STANDARD 23)
file(GLOB headers CONFIGURE_DEPENDS src/*.hpp)
file(GLOB sources CONFIGURE_DEPENDS src/*.cpp)

# Fetures
option(CHECK "Setup checking and fuzzing" OFF)
option(PROOF "Check SAT solver proofs" OFF)
set(SAT "kissat" CACHE STRING "SAT solver to use: kissat or cadical")
set_property(CACHE SAT PROPERTY STRINGS kissat cadical)

# Compile time
option(STATIC "Build static binary" OFF)
option(LTO "Enable Link-Time Optimization" OFF)
option(STRICT "Enable strict compilation options" ON)
option(CLANG "Use clang/clang++ compilers" OFF)

# Runtime
option(ASSERT "Enable runtime assertions (even in Release)" OFF)
option(ASAN "Build with address sanitizer" OFF)
option(UBSAN "Build with undefined behavior sanitizer" OFF)

add_executable(certifaiger ${sources})
util_add_options(certifaiger)
target_compile_options(certifaiger PRIVATE -DGITID="${GIT_ID}")
target_compile_options(certifaiger PRIVATE -DVERSION="${PROJECT_VERSION}")
install(TARGETS certifaiger)
include(deps/aiger/aiger_lib.cmake)
target_link_libraries(certifaiger aiger)

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
    certifuzzer
    annotate)

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
endif()
