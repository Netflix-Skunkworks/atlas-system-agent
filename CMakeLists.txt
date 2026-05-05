cmake_minimum_required(VERSION 3.23)

project(atlas-system-agent)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

add_compile_options(-pedantic -Werror -Wall -Wno-missing-braces -fno-omit-frame-pointer)

include(CTest)

find_package(absl REQUIRED)
find_package(asio REQUIRED)
find_package(Backward REQUIRED)
find_package(Boost REQUIRED)
find_package(fmt REQUIRED)
find_package(GTest REQUIRED)
find_package(CURL REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(RapidJSON REQUIRED)
find_package(sdbus-c++ REQUIRED)
find_package(spdlog REQUIRED)
find_package(ZLIB REQUIRED)

option(TITUS_SYSTEM_SERVICE "Titus System Service" OFF)
if(TITUS_SYSTEM_SERVICE)
    message(STATUS "building atlas-titus-agent")
    add_definitions(-DTITUS_SYSTEM_SERVICE)
else()
    message(STATUS "building atlas-system-agent")
endif()

add_subdirectory(thirdparty/spectator-cpp)

# Build AMD SMI from vendored source (fetched by Conan source phase).
# Disables parts we don't need to keep build time down and to avoid
# touching system locations (e.g., ldconfig).
#
# Note: the function only scopes regular variables; the set(... CACHE FORCE)
# calls below still write to the global cache. If AMD SMI's option names ever
# collide with our own (e.g., BUILD_TESTS), or we add another vendored
# subproject that shares names, switch this to ExternalProject_Add for a
# fully isolated CMake invocation with its own cache.
function(add_amdsmi_subdirectory)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(BUILD_CLI OFF CACHE BOOL "" FORCE)
    set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(BUILD_WRAPPER OFF CACHE BOOL "" FORCE)
    set(BUILD_RUST_WRAPPER OFF CACHE BOOL "" FORCE)
    set(ENABLE_LDCONFIG OFF CACHE BOOL "" FORCE)
    add_subdirectory(${CMAKE_SOURCE_DIR}/thirdparty/rocm-systems/projects/amdsmi
                     ${CMAKE_BINARY_DIR}/amdsmi-build)
endfunction()
add_amdsmi_subdirectory()

add_subdirectory(lib)
add_subdirectory(AtlasAgent)