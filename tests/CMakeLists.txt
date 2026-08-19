#
# Catch2 is taken from the system when it is installed, otherwise it is fetched.
#
find_package(Catch2 3 QUIET)

if(NOT Catch2_FOUND)
  include(FetchContent)
  FetchContent_Declare(Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.7.1
  )
  FetchContent_MakeAvailable(Catch2)
  list(APPEND CMAKE_MODULE_PATH "${catch2_SOURCE_DIR}/extras")
endif()

include(Catch)

#
# Warning flags, the library has to stay clean under them.
#
if(MSVC)
  set(SFSM_TEST_WARNINGS /W4 /WX /permissive-)
else()
  set(SFSM_TEST_WARNINGS -Wall -Wextra -Wpedantic -Werror)
endif()

file(GLOB SFSM_TEST_FILES CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/*.cpp")

add_executable(sfsm_test ${SFSM_TEST_FILES})
target_link_libraries(sfsm_test PRIVATE sfsm::sfsm Catch2::Catch2WithMain)
target_include_directories(sfsm_test PRIVATE "${CMAKE_CURRENT_LIST_DIR}")
target_compile_options(sfsm_test PRIVATE ${SFSM_TEST_WARNINGS})

catch_discover_tests(sfsm_test)

#
# Every header has to be includable on its own. One translation unit per header is generated
# during configuration, so a missing include shows up as a build error and not in a consumer.
#
file(GLOB SFSM_HEADERS RELATIVE "${PROJECT_SOURCE_DIR}/include" CONFIGURE_DEPENDS
  "${PROJECT_SOURCE_DIR}/include/sfsm/*.hpp"
)

set(SFSM_SELF_CONTAINED_FILES "")
foreach(header IN LISTS SFSM_HEADERS)
  string(REGEX REPLACE "[/.]" "_" name "${header}")
  set(generated "${CMAKE_CURRENT_BINARY_DIR}/self_contained/${name}.cpp")
  file(WRITE "${generated}" "#include <${header}>\n")
  list(APPEND SFSM_SELF_CONTAINED_FILES "${generated}")
endforeach()

add_library(sfsm_self_contained OBJECT ${SFSM_SELF_CONTAINED_FILES})
target_link_libraries(sfsm_self_contained PRIVATE sfsm::sfsm)
target_compile_options(sfsm_self_contained PRIVATE ${SFSM_TEST_WARNINGS})
