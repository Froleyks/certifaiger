cmake_minimum_required(VERSION 3.22.1)

project(certifaiger)

#--------------------------------------------------------------------------------------------------
# Configurable options:

option(LTO                   "Enable Link-Time Optimization"                            ON )
option(STATIC_BINARIES       "Link binaries statically."                                ON )
option(FULLY_STATIC_BINARIES "Link binaries fully statically (including libc++, etc.)." ON )
option(USE_SORELEASE         "Use SORELEASE in shared library filename."                ON )

#--------------------------------------------------------------------------------------------------
# Library version:

set(CERTIFAIGER_SOMAJOR   0)
set(CERTIFAIGER_SOMINOR   1)
set(CERTIFAIGER_SORELEASE 0)

# Compute VERSION and SOVERSION:
if (USE_SORELEASE)
  set(CERTIFAIGER_VERSION ${CERTIFAIGER_SOMAJOR}.${CERTIFAIGER_SOMINOR}.${CERTIFAIGER_SORELEASE})
else()
  set(CERTIFAIGER_VERSION ${CERTIFAIGER_SOMAJOR}.${CERTIFAIGER_SOMINOR})
endif()
set(CERTIFAIGER_SOVERSION ${CERTIFAIGER_SOMAJOR})

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

if(FULLY_STATIC_BINARIES)
  set(CERTIFAIGER_BIN_LINK "-static")
  set(STATIC_BINARIES ON)
else(FULLY_STATIC_BINARIES)
  set(CERTIFAIGER_BIN_LINK "")
endif(FULLY_STATIC_BINARIES)

if(STATIC_BINARIES)
  set(CERTIFAIGER_LIB_LINK "static")
else(STATIC_BINARIES)
  set(CERTIFAIGER_LIB_LINK "shared")
endif(STATIC_BINARIES)

add_executable(certifaiger certifaiger.cpp)
target_link_libraries(certifaiger aiger-lib-${CERTIFAIGER_LIB_LINK})
target_link_options(certifaiger PRIVATE ${CERTIFAIGER_BIN_LINK})
set_target_properties(certifaiger PROPERTIES OUTPUT_NAME "certifaiger")
install(TARGETS certifaiger)
