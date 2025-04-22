cmake_minimum_required(VERSION 3.23)

project(atlas-system-agent)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

add_compile_options(-pedantic -Werror -Wall -Wno-missing-braces -fno-omit-frame-pointer "$<$<CONFIG:Debug>:-fsanitize=address>")

include(CTest)

find_package(absl REQUIRED)
find_package(asio REQUIRED)
find_package(Backward REQUIRED)
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

add_subdirectory(lib)

#-- atlas_system_agent executable
add_executable(atlas_system_agent "bin/atlas-agent.cc")
target_link_libraries(atlas_system_agent sysagent)
# required to allow running on older systems, such as bionic
target_link_options(atlas_system_agent PRIVATE "-static-libstdc++")
