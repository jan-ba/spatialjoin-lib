cmake_minimum_required (VERSION 3.21)

project (spatialjoin)

option(SPATIALJOIN_BUILD_CLI "Build the spatialjoin command-line executable" ${PROJECT_IS_TOP_LEVEL})
option(SPATIALJOIN_BUILD_TESTS "Build SpatialJoin tests" ${PROJECT_IS_TOP_LEVEL})

if (CMAKE_BUILD_TYPE)
	string(TOUPPER ${CMAKE_BUILD_TYPE} CMAKE_BUILD_TYPE)
endif()

if(PROJECT_IS_TOP_LEVEL AND SPATIALJOIN_BUILD_TESTS)
	enable_testing()
endif()

if(NOT SPATIALJOIN_BUILD_TESTS)
	set(CMAKE_TESTING_ENABLED OFF)
endif()

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_CURRENT_SOURCE_DIR}/cmake/")

set(CMAKE_CXX_STANDARD 14 CACHE STRING "C++ standard to build with")
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# set compiler flags, see http://stackoverflow.com/questions/7724569/debug-vs-release-in-cmake
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -fno-signed-zeros -fno-trapping-math -Wall -Wno-format-extra-args -Wextra -Wformat-nonliteral -Wformat-security -Wformat=2 -Wextra -Wno-implicit-fallthrough -Wnarrowing -pedantic")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O3 -fno-signed-zeros -fno-trapping-math -Wall -Wno-format-extra-args -Wextra -Wformat-nonliteral -Wformat-security -Wformat=2 -Wextra -Wno-implicit-fallthrough -Wnarrowing -pedantic")

set(CMAKE_CXX_FLAGS_DEBUG          "-Og -g -DLOGLEVEL=3")
set(CMAKE_CXX_FLAGS_MINSIZEREL     "${CMAKE_CXX_FLAGS} -DLOGLEVEL=2")
set(CMAKE_CXX_FLAGS_RELEASE        "${CMAKE_CXX_FLAGS} -DLOGLEVEL=2")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS} -g -DLOGLEVEL=3")

find_package(BZip2)
find_package(ZLIB)

if (NOT BZIP2_FOUND)
	add_definitions( -DSPATIALJOIN_NO_BZIP2=1 )
endif()

if (NOT ZLIB_FOUND)
	add_definitions( -DSPATIALJOIN_NO_ZLIB=1 )
endif()

# export compile commands to tools like clang
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# http://brianmilco.blogspot.de/2012/11/cmake-automatically-use-git-tags-as.html
include(GetGitRevisionDescription OPTIONAL RESULT_VARIABLE _get_git_revision)
if (_get_git_revision)
	git_get_tag(VERSION_GIT)
	get_git_is_dirty(VERSION_GIT_IS_DIRTY)
	if ("${VERSION_GIT_IS_DIRTY}" STREQUAL "")
		set(VERSION_GIT_FULL "${VERSION_GIT}")
	else()
		set(VERSION_GIT_FULL "${VERSION_GIT}-${VERSION_GIT_IS_DIRTY}")
	endif()
endif()

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

add_subdirectory(src)

install(TARGETS spatialjoin-dev pb_util pb_util_geo
    EXPORT spatialjoinTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(FILES src/spatialjoin/Join.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/spatialjoin)
install(DIRECTORY src/util/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/util
    FILES_MATCHING
    PATTERN "*.h"
    PATTERN "*.hpp"
    PATTERN "*.tpp"
    PATTERN ".git*" EXCLUDE
    PATTERN "tests" EXCLUDE)
install(FILES LICENSE
    DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/spatialjoin)
install(FILES src/util/LICENSE
    DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/spatialjoin
    RENAME util-LICENSE)

set(SPATIALJOIN_CMAKE_DIR ${CMAKE_INSTALL_LIBDIR}/cmake/spatialjoin)
configure_package_config_file(
    cmake/spatialjoinConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/spatialjoinConfig.cmake
    INSTALL_DESTINATION ${SPATIALJOIN_CMAKE_DIR})
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/spatialjoinConfig.cmake
    DESTINATION ${SPATIALJOIN_CMAKE_DIR})
install(EXPORT spatialjoinTargets
    NAMESPACE spatialjoin::
    DESTINATION ${SPATIALJOIN_CMAKE_DIR})

if(SPATIALJOIN_BUILD_CLI)
    install(TARGETS spatialjoin RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
