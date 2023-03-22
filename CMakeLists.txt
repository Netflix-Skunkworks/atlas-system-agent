cmake_minimum_required(VERSION 3.13)

project(atlas-system-agent)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

add_compile_options(-fno-omit-frame-pointer "$<$<CONFIG:Debug>:-fsanitize=address>")
add_link_options(-fno-omit-frame-pointer "$<$<CONFIG:Debug>:-fsanitize=address>")

include(CTest)
include(${CMAKE_BINARY_DIR}/conanbuildinfo.cmake)
conan_basic_setup()

option(TITUS_SYSTEM_SERVICE "Titus System Service" OFF)
if(TITUS_SYSTEM_SERVICE)
    message(STATUS "building atlas-titus-agent")
    add_definitions(-DTITUS_SYSTEM_SERVICE)
else()
    message(STATUS "building atlas-system-agent")
endif()

add_subdirectory(contain)
add_subdirectory(lib)

#-- sysagent_test test executable
file(GLOB sysagent_test_source_files
    "bin/runtests.cc"
    "lib/*test.cc"
    "lib/sample_config.cc"
    "lib/measurement_utils.*"
)
add_executable(sysagent_test ${sysagent_test_source_files})
target_include_directories(sysagent_test PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(sysagent_test sysagent ${CONAN_LIBS})
add_test(
    NAME sysagent_test
    COMMAND sysagent_test
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

#-- atlas_system_agent executable
add_executable(atlas_system_agent "bin/atlas-agent.cc")
target_include_directories(atlas_system_agent PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/lib
)
target_link_libraries(atlas_system_agent sysagent ${CONAN_LIBS})
target_link_options(atlas_system_agent PRIVATE
    "$<$<CONFIG:Release>:-static-libstdc++>"
)
