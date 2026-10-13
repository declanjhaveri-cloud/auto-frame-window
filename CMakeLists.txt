cmake_minimum_required(VERSION 3.25)
project(auto_frame_window)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(ARC_FEATURE_FULL ON CACHE BOOL "" FORCE)

if(NOT DEFINED ENV{GEODE_SDK})
    message(FATAL_ERROR "Unable to find Geode SDK! Please define GEODE_SDK environment variable to point to Geode")
endif()
add_subdirectory($ENV{GEODE_SDK} ${CMAKE_CURRENT_BINARY_DIR}/geode)

# CPM
set(CPM_DOWNLOAD_VERSION 0.40.2)
if(NOT EXISTS "${CMAKE_CURRENT_BINARY_DIR}/cmake/CPM_${CPM_DOWNLOAD_VERSION}.cmake")
    file(DOWNLOAD
        https://github.com/cpm-cmake/CPM.cmake/releases/download/v${CPM_DOWNLOAD_VERSION}/CPM.cmake
        "${CMAKE_CURRENT_BINARY_DIR}/cmake/CPM_${CPM_DOWNLOAD_VERSION}.cmake"
        STATUS DOWNLOAD_STATUS
    )
    list(GET DOWNLOAD_STATUS 0 STATUS_CODE)
    if(NOT STATUS_CODE EQUAL 0)
        message(FATAL_ERROR "Failed to download CPM.cmake: ${DOWNLOAD_STATUS}")
    endif()
endif()
include("${CMAKE_CURRENT_BINARY_DIR}/cmake/CPM_${CPM_DOWNLOAD_VERSION}.cmake")

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "src/*.cpp" "src/*.hpp")
add_library(${PROJECT_NAME} SHARED ${SOURCES})
setup_geode_mod(${PROJECT_NAME})

# GitHub build has the in-game update checker; the Geode index build turns it off.
option(AFW_SELF_UPDATE "Include the GitHub update checker" ON)
if(AFW_SELF_UPDATE)
    target_compile_definitions(${PROJECT_NAME} PRIVATE AFW_SELF_UPDATE=1)
endif()

# GDR replay format + converter for older .gdr / .gdr.json (same versions Frame Window Counter uses)
CPMAddPackage("gh:maxnut/GDReplayFormat#2a0ca3c")
CPMAddPackage(
    NAME msgpack-for-matjson
    GITHUB_REPOSITORY prevter/msgpack-for-matjson
    GIT_TAG main
)
CPMAddPackage(
    NAME GDRconverter
    GITHUB_REPOSITORY maxnut/GDR-converter
    GIT_TAG main
    EXCLUDE_FROM_ALL YES
    OPTIONS "BUILD_TESTING OFF"
)

target_link_libraries(${PROJECT_NAME} libGDR GDRconverter)
