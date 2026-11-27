cmake_minimum_required(VERSION 3.21)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)

# XFC 2.3.0
project(XFC VERSION 2.3.0)

# All .cpp under src/
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS src/*.cpp)

add_library(${PROJECT_NAME} SHARED ${SOURCES})

# Headers in src/ → #include "myheader.hpp"
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

if (NOT DEFINED ENV{GEODE_SDK})
    message(FATAL_ERROR "GEODE_SDK is not set. Export it to your Geode SDK path.")
endif()
message(STATUS "Found Geode SDK: $ENV{GEODE_SDK}")

# Build codegen from pinned bindings (needed for Geode 4.10 + GD 2.2074 on CI)
set(SKIP_BUILDING_CODEGEN OFF CACHE BOOL "" FORCE)

add_subdirectory($ENV{GEODE_SDK} ${CMAKE_CURRENT_BINARY_DIR}/geode)

setup_geode_mod(${PROJECT_NAME})

# Re-apply after setup_geode_mod (some SDK versions reset includes)
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
