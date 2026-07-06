cmake_minimum_required(VERSION 3.19)

# Features
option(CHECK "Setup checking and fuzzing" OFF)
set(SAT "kissat" CACHE STRING "SAT solver to use; kissat and cadical are built automatically")
set_property(CACHE SAT PROPERTY STRINGS kissat cadical)
set(PROOF "" CACHE STRING "Proof checker to use; dpr-trim, lrat-trim, and lrat_isa are built automatically")
set_property(CACHE PROOF PROPERTY STRINGS "" dpr-trim lrat-trim lrat_isa)

# Compile time
option(STATIC "Build static binary" OFF)
option(LTO "Enable Link-Time Optimization" OFF)
option(STRICT "Enable strict compilation options" ON)
option(CLANG "Use clang/clang++ compilers" OFF)

# Runtime
option(ASSERT "Enable runtime assertions (even in Release)" OFF)
option(ASAN "Build with address sanitizer" OFF)
option(UBSAN "Build with undefined behavior sanitizer" OFF)

include(util.cmake)
project(certifaiger VERSION 10.2.0)
set(CMAKE_CXX_STANDARD 23)
file(GLOB headers CONFIGURE_DEPENDS src/*.hpp)
file(GLOB sources CONFIGURE_DEPENDS src/*.cpp)

add_executable(certifaiger ${sources})
util_add_options(certifaiger)
target_compile_definitions(certifaiger PRIVATE
  GITID="${GIT_ID}"
  VERSION="${PROJECT_VERSION}")
install(TARGETS certifaiger)
include(deps/aiger/aiger_lib.cmake)
target_link_libraries(certifaiger aiger)

if(CHECK)
  include(deps/aiger/aiger_bin.cmake)
  include(deps/runlim/runlim_bin.cmake)
  if(SAT STREQUAL "kissat")
    include(deps/kissat/kissat_bin.cmake)
  elseif(SAT STREQUAL "cadical")
    include(deps/cadical/cadical_bin.cmake)
  else()
    message(WARNING "Not building SAT solver ${SAT}, place binary in bin directory")
  endif()
  if(PROOF)
    if(PROOF STREQUAL "dpr-trim")
      include(deps/dpr-trim/dpr-trim_bin.cmake)
      if(NOT SAT STREQUAL "kissat")
        message(WARNING "PROOF=dpr-trim is intended for SAT=kissat, but SAT=${SAT}")
      endif()
    elseif(PROOF STREQUAL "lrat-trim")
      include(deps/lrat-trim/lrat-trim_bin.cmake)
      if(NOT SAT STREQUAL "cadical")
        message(WARNING "PROOF=lrat-trim is intended for SAT=cadical, but SAT=${SAT}")
      endif()
    elseif(PROOF STREQUAL "lrat_isa")
      include(deps/lrat_isa/lrat_isa_bin.cmake)
      if(NOT SAT STREQUAL "cadical")
        message(WARNING "PROOF=lrat_isa is intended for SAT=cadical, but SAT=${SAT}")
      endif()
    else()
      message(WARNING "Not building proof checker ${PROOF}, place binary in bin directory")
    endif()
  endif()
  add_scripts(
    check
    check_unsat
    check_sat
    certified
    limit
    random
    fuzz
    status
    certifuzzer
    annotate)
endif()
