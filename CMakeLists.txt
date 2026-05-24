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

# Build AMD SMI as an ExternalProject — runs in its own isolated CMake
# invocation with its own cache, its own compile flags, and no inheritance
# of our parent project's -pedantic -Werror -Wall (which AMD SMI's code
# does not survive).
include(ExternalProject)

function(add_amdsmi_external_project)
    set(_source_dir  ${CMAKE_SOURCE_DIR}/thirdparty/rocm-systems/projects/amdsmi)
    set(_install_dir ${CMAKE_BINARY_DIR}/amdsmi-install)
    set(_library     ${_install_dir}/lib/libamd_smi.a)

    ExternalProject_Add(amdsmi_external
        SOURCE_DIR ${_source_dir}
        PREFIX     ${CMAKE_BINARY_DIR}/amdsmi
        INSTALL_DIR ${_install_dir}
        CMAKE_ARGS
            -DCMAKE_BUILD_TYPE=$<CONFIG>
            -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
            -DCMAKE_INSTALL_LIBDIR=lib
            -DCMAKE_POSITION_INDEPENDENT_CODE=ON
            -DBUILD_SHARED_LIBS=OFF
            -DBUILD_CLI=OFF
            -DBUILD_TESTS=OFF
            -DBUILD_EXAMPLES=OFF
            -DBUILD_WRAPPER=OFF
            -DBUILD_RUST_WRAPPER=OFF
            -DENABLE_LDCONFIG=OFF
            -DENABLE_ESMI_LIB=OFF
        BUILD_BYPRODUCTS ${_library}
    )

    # Headers exist in source tree at configure time; they're also installed
    # at build time, but pointing at source avoids waiting for the build.
    file(MAKE_DIRECTORY ${_source_dir}/include)

    # IMPORTED target so anything else in this build tree can link to AMD
    # SMI the same way it would link to any normal CMake target.
    add_library(amd_smi STATIC IMPORTED GLOBAL)
    set_target_properties(amd_smi PROPERTIES
        IMPORTED_LOCATION ${_library}
        INTERFACE_INCLUDE_DIRECTORIES ${_source_dir}/include
    )
    add_dependencies(amd_smi amdsmi_external)
endfunction()
add_amdsmi_external_project()

add_subdirectory(lib)
add_subdirectory(AtlasAgent)