include(FetchContent)
FetchContent_Declare(aiger GIT_REPOSITORY https://github.com/arminbiere/aiger
                     PATCH_COMMAND cp ${CMAKE_CURRENT_LIST_DIR}/CMakeLists.txt .)
FetchContent_MakeAvailable(aiger)
