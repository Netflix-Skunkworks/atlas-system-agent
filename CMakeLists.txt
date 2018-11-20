cmake_minimum_required(VERSION 2.8)
cmake_minimum_required(VERSION 2.8)

set(PROJECT_NAME_STR atlas-system-agent)
set(CMAKE_MACOSX_RPATH 1)
set(CMAKE_EXPORT_COMPILE_COMMANDS "ON")
project(${PROJECT_NAME_STR})

if(CMAKE_SYSTEM_NAME STREQUAL Linux)
  set(DSO_EXT "so")
else()
  set(DSO_EXT "dylib")
endif()
option (TITUS_AGENT
        "Build an agent for titus (ON) or the system agent (OFF)"
        OFF)

configure_file("${PROJECT_SOURCE_DIR}/config.h.in" "${PROJECT_SOURCE_DIR}/config.h")

set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_BUILD_TYPE, RelWithDebInfo)

include_directories(3rd-party /usr/local/include contain ${PROJECT_SOURCE_DIR}/nc/root/include)
link_directories(/usr/local/lib ${PROJECT_SOURCE_DIR}/nc/root/lib)

file(GLOB LIB_SOURCE_FILES lib/*.cc lib/*.h)
if(CMAKE_SYSTEM_NAME STREQUAL Linux)
  link_directories(/usr/lib/x86_64-linux-gnu)
  add_library(contain OBJECT contain/contain.c)
  target_compile_options(contain PUBLIC -Wall -Wextra -std=c11 -pedantic-errors -Werror)
  add_library(atlasagent STATIC ${LIB_SOURCE_FILES} $<TARGET_OBJECTS:contain>)
else()
  add_library(atlasagent SHARED ${LIB_SOURCE_FILES})
endif()

set_target_properties(atlasagent PROPERTIES COMPILE_FLAGS "${CMAKE_CXX_FLAGS} -Wextra -Wno-missing-braces")

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DFMT_HEADER_ONLY -DSPDLOG_FMT_EXTERNAL -fno-rtti -pedantic -Werror -Wall -Wno-missing-braces -std=c++11")

set(APP_SOURCE_FILES atlas-agent.cc)
add_executable(atlas-system-agent ${APP_SOURCE_FILES})
if(CMAKE_SYSTEM_NAME STREQUAL Linux)
  target_link_libraries(atlasagent libspectator_cpp.a)
  target_link_libraries(atlas-system-agent atlasagent libspectator_cpp.a libspectator_curl.a z pthread dl -static-libgcc -static-libstdc++)
else()
  target_link_libraries(atlasagent spectator_cpp)
  target_link_libraries(atlas-system-agent atlasagent spectator_cpp)
endif()

# test configuration
enable_testing()
file(GLOB TEST_SOURCE_FILES test/*.cc)

if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
  add_executable(test_nvml test_nvml.cc)
  target_link_libraries(test_nvml atlasagent libspectator_cpp.a libspectator_curl.a z pthread dl -static-libgcc -static-libstdc++)
endif ()

add_executable(runtests ${TEST_SOURCE_FILES})
set_target_properties(runtests PROPERTIES COMPILE_FLAGS "${CMAKE_CXX_FLAGS} -Wno-sign-compare")

add_library(gtest 3rd-party/src/gtest/gtest-all.cc)

target_link_libraries(runtests atlasagent spectator_cpp gtest pthread z)
add_test(runtests runtests)

add_custom_command(TARGET runtests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/test/resources2 $<TARGET_FILE_DIR:runtests>/resources2)
add_custom_command(TARGET runtests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/test/resources $<TARGET_FILE_DIR:runtests>/resources)

if (TITUS_AGENT)
  target_link_libraries(atlas-system-agent libcap.a)
  set_target_properties(atlas-system-agent PROPERTIES OUTPUT_NAME "atlas-titus-agent")
else()
  install(PROGRAMS scripts/start-atlas-agent DESTINATION bin)
endif (TITUS_AGENT)

install(TARGETS atlas-system-agent DESTINATION bin)

