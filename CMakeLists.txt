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

include_directories(3rd-party /usr/local/include xfs contain ${PROJECT_SOURCE_DIR}/nc/root/include)
link_directories(/usr/local/lib ${PROJECT_SOURCE_DIR}/nc/root/lib)

file(GLOB LIB_SOURCE_FILES lib/*.cc lib/*.h)
if(CMAKE_SYSTEM_NAME STREQUAL Linux)
  file(GLOB BTRFS_SOURCE_FILES 3rd-party/btrfs/*.c)
  add_library(atlasbtrfs ${BTRFS_SOURCE_FILES})

  link_directories(/usr/lib/x86_64-linux-gnu)
  add_library(contain OBJECT contain/contain.c)
  add_library(xfsquota OBJECT xfs/xfsquota.c)
  target_compile_options(contain PUBLIC -Wall -Wextra -std=c11 -pedantic-errors -Werror)
  target_compile_options(xfsquota PUBLIC -Wall -Wextra -std=c11 -pedantic-errors -Werror)
  add_library(atlasagent STATIC ${LIB_SOURCE_FILES} $<TARGET_OBJECTS:contain> $<TARGET_OBJECTS:xfsquota>)

else()
  add_library(atlasagent SHARED ${LIB_SOURCE_FILES})
endif()

set_target_properties(atlasagent PROPERTIES COMPILE_FLAGS "${CMAKE_CXX_FLAGS} -Wextra -Wno-missing-braces")

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DFMT_HEADER_ONLY -DSPDLOG_FMT_EXTERNAL -fno-rtti -Werror -Wall -Wno-missing-braces -std=c++11 -DBTRFS_FLAT_INCLUDES=1")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DBTRFS_FLAT_INCLUDES=1")

set(APP_SOURCE_FILES atlas-agent.cc)
add_executable(atlas-system-agent ${APP_SOURCE_FILES})
if(CMAKE_SYSTEM_NAME STREQUAL Linux)
  target_link_libraries(atlasagent libatlasclient.a atlasbtrfs)
  target_link_libraries(atlas-system-agent atlasagent libatlasclient.a libatlascurl.a libatlaspcre.a z pthread dl -static-libgcc -static-libstdc++)
else()
  target_link_libraries(atlasagent atlasclient)
  target_link_libraries(atlas-system-agent atlasagent atlasclient)
endif()

# test configuration
enable_testing()
file(GLOB TEST_SOURCE_FILES test/*.cc)

if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
  add_executable(test_nvml test_nvml.cc)
  target_link_libraries(test_nvml atlasagent libatlasclient.a libatlascurl.a libatlaspcre.a z pthread dl -static-libgcc -static-libstdc++)
endif ()

add_executable(runtests ${TEST_SOURCE_FILES})
set_target_properties(runtests PROPERTIES COMPILE_FLAGS "${CMAKE_CXX_FLAGS} -Wno-sign-compare")

add_library(gtest 3rd-party/src/gtest/gtest-all.cc)

if(CMAKE_SYSTEM_NAME STREQUAL Linux)
  target_link_libraries(runtests atlasagent atlasbtrfs atlasclient gtest pthread z)
else()
  target_link_libraries(runtests atlasagent atlasclient gtest pthread z)
endif()
add_test(runtests runtests)

add_custom_command(TARGET runtests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/test/resources2 $<TARGET_FILE_DIR:runtests>/resources2)
add_custom_command(TARGET runtests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/test/resources $<TARGET_FILE_DIR:runtests>/resources)

if (TITUS_AGENT)
  set_target_properties(atlas-system-agent PROPERTIES OUTPUT_NAME "atlas-titus-agent")
endif (TITUS_AGENT)
install(TARGETS atlas-system-agent DESTINATION bin)
install(PROGRAMS scripts/start-atlas-agent DESTINATION bin)
