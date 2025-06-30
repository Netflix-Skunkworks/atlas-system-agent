cmake_minimum_required(VERSION 3.23)

project(atlas-system-agent)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

add_compile_options(-pedantic -Werror -Wall -Wno-missing-braces -fno-omit-frame-pointer "$<$<CONFIG:Debug>:-fsanitize=address>")

# Apply sanitizer flags globally in Debug mode
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_options(-fsanitize=address)
    add_link_options(-fsanitize=address)
endif()

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

# Add spectator-cpp as a subdirectory (CMake-ified)
add_subdirectory(thirdparty/spectator-cpp)

add_subdirectory(lib)
#add_subdirectory(AtlasAgent)