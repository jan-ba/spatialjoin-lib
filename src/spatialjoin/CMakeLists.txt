file(GLOB_RECURSE SPATIALJOIN_SRC *.cpp)
list(FILTER SPATIALJOIN_SRC EXCLUDE REGEX ".*/tests/.*")
list(FILTER SPATIALJOIN_SRC EXCLUDE REGEX ".*/SpatialJoinMain\\.cpp$")

if(PROJECT_IS_TOP_LEVEL)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
endif()

set(spatialjoin_main SpatialJoinMain.cpp)

if(SPATIALJOIN_BUILD_CLI)
    add_executable(spatialjoin ${spatialjoin_main})
endif()
add_library(spatialjoin-dev STATIC ${SPATIALJOIN_SRC})
add_library(spatialjoin::spatialjoin ALIAS spatialjoin-dev)
set_target_properties(spatialjoin-dev PROPERTIES OUTPUT_NAME spatialjoin)
set_target_properties(spatialjoin-dev PROPERTIES EXPORT_NAME spatialjoin)
find_package(Threads REQUIRED)

if(SPATIALJOIN_BUILD_CLI)
    target_include_directories(spatialjoin PUBLIC
        $<BUILD_INTERFACE:${SPATIALJOIN_INCLUDE_DIR}>
    )
endif()

target_include_directories(spatialjoin-dev PUBLIC
	$<BUILD_INTERFACE:${SPATIALJOIN_INCLUDE_DIR}>
	$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_compile_features(spatialjoin-dev PUBLIC cxx_std_14)
target_link_libraries(spatialjoin-dev PUBLIC pb_util pb_util_geo Threads::Threads)
if(SPATIALJOIN_BUILD_CLI)
    target_link_libraries(spatialjoin PRIVATE spatialjoin-dev)
endif()

if (BZIP2_FOUND)
	if(SPATIALJOIN_BUILD_CLI)
        target_include_directories(spatialjoin PUBLIC ${BZIP2_INCLUDE_DIR} )
        target_link_libraries(spatialjoin PRIVATE ${BZIP2_LIBRARIES} )
    endif()
	target_include_directories(spatialjoin-dev PUBLIC ${BZIP2_INCLUDE_DIR} )
endif(BZIP2_FOUND)

if (ZLIB_FOUND)
	if(SPATIALJOIN_BUILD_CLI)
        target_include_directories(spatialjoin INTERFACE ${ZLIB_INCLUDE_DIRS} )
        target_link_libraries(spatialjoin PRIVATE ${ZLIB_LIBRARIES} )
    endif()
	target_include_directories(spatialjoin-dev INTERFACE ${ZLIB_INCLUDE_DIRS} )
endif(ZLIB_FOUND)


if(CMAKE_TESTING_ENABLED)
    add_subdirectory(tests)
endif()

if(PROJECT_IS_TOP_LEVEL AND CMAKE_TESTING_ENABLED)
	add_test(NAME "utilTest" COMMAND utilTest)
endif()
