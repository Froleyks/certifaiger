cmake_minimum_required(VERSION 3.22.1)
project(certifaiger)

#--------------------------------------------------------------------------------------------------
# Configurable options:
option(LTO                   "Enable Link-Time Optimization"                            ON )
option(FULLY_STATIC_BINARIES "Link binaries fully statically (including libc++, etc.)." ON )

#--------------------------------------------------------------------------------------------------
# Library:
set(AIGER_PATH "$ENV{AIGER}" CACHE PATH "Path to the aiger directory")
if(NOT AIGER_PATH)
  set(AIGER_PATH "../aiger")
endif()
find_file(AIGER_SRC NAMES aiger.c PATHS ${AIGER_PATH})
if(NOT AIGER_SRC)
  message(FATAL_ERROR "Aiger library not found in ${AIGER_PATH}.
    Please set with -DAIGER_PATH=/path/to/aiger or build_dependencies.sh to download to the default location.")
endif()

#--------------------------------------------------------------------------------------------------
# Compile flags:
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(LTO)
  set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
endif(LTO)

#--------------------------------------------------------------------------------------------------
# Executable Targets:
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" VERSION)
string(STRIP ${VERSION} VERSION)
execute_process(COMMAND bash "-c" "git show|head -1|awk '{print $2}'" OUTPUT_STRIP_TRAILING_WHITESPACE OUTPUT_VARIABLE GIT_ID)

add_definitions("-DNO_BUILD_HPP=1")
add_definitions("-DVERSION=\"${VERSION}\"")
add_definitions("-DGITID=\"${GIT_ID}\"")

add_executable(certifaiger certifaiger.cpp)
add_library(aiger STATIC ${AIGER_PATH}/aiger.c)
target_include_directories(aiger PUBLIC ${AIGER_PATH}/)
target_link_libraries(certifaiger aiger)

if(FULLY_STATIC_BINARIES)
  set(CERTIFAIGER_BIN_LINK "-static")
  set(STATIC_BINARIES ON)
else(FULLY_STATIC_BINARIES)
  set(CERTIFAIGER_BIN_LINK "")
endif(FULLY_STATIC_BINARIES)
target_link_options(certifaiger PRIVATE ${CERTIFAIGER_BIN_LINK})

install(TARGETS certifaiger)
