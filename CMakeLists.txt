cmake_minimum_required(VERSION 3.23)

project(sfsm
  VERSION 0.1.0
  DESCRIPTION "Simple compile time finite state machine with data carrying states"
  HOMEPAGE_URL "https://github.com/michael-b3n/SFSM"
  LANGUAGES CXX
)

#
# Only build the tests and provide the install rules when SFSM is the project being built. A
# consumer that pulls SFSM in with FetchContent or add_subdirectory gets the target and nothing else.
#
option(SFSM_BUILD_TESTS "Build the SFSM tests" ${PROJECT_IS_TOP_LEVEL})
option(SFSM_INSTALL "Generate the SFSM install rules" ${PROJECT_IS_TOP_LEVEL})

#
# The default install prefix of CMake is a system location and needs the rights that come with it.
# A library that is installed to look at it does not, so an own build stays inside the build tree.
# Anything given explicitly wins, be it -DCMAKE_INSTALL_PREFIX at configure time or --prefix at
# install time, the default below only applies when nothing was given at all.
#
if(PROJECT_IS_TOP_LEVEL AND CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
  set(CMAKE_INSTALL_PREFIX "${CMAKE_BINARY_DIR}/install"
    CACHE PATH "Directory SFSM is installed to" FORCE
  )
endif()

#
# The library itself, header only.
#
file(GLOB SFSM_HEADER_FILES CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/include/sfsm/*.hpp")

add_library(sfsm INTERFACE)
add_library(sfsm::sfsm ALIAS sfsm)

target_compile_features(sfsm INTERFACE cxx_std_20)
target_sources(sfsm
  INTERFACE
  FILE_SET HEADERS
  BASE_DIRS "${CMAKE_CURRENT_LIST_DIR}/include"
  FILES ${SFSM_HEADER_FILES}
)

if(SFSM_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

if(SFSM_INSTALL)
  include(GNUInstallDirs)
  include(CMakePackageConfigHelpers)

  install(TARGETS sfsm
    EXPORT sfsm-targets
    FILE_SET HEADERS DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
  )
  install(EXPORT sfsm-targets
    FILE sfsm-targets.cmake
    NAMESPACE sfsm::
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/sfsm"
  )

  configure_package_config_file(
    "${CMAKE_CURRENT_LIST_DIR}/cmake/sfsm-config.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/sfsm-config.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/sfsm"
  )
  write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/sfsm-config-version.cmake"
    COMPATIBILITY SameMajorVersion
    ARCH_INDEPENDENT
  )
  install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/sfsm-config.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/sfsm-config-version.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/sfsm"
  )
endif()
