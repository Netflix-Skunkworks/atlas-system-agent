add_library(tagging INTERFACE
    src/tagger.h
    src/tagging_registry.h
)

target_include_directories(tagging
INTERFACE ${CMAKE_SOURCE_DIR}
)

# Add any dependencies if needed
target_link_libraries(tagging
INTERFACE
    fmt::fmt
    rapidjson
)

# Add tagger test executable
add_executable(tagger_test
    test/tagger_test.cc
)

target_link_libraries(tagger_test
    tagging
    logger
    gtest::gtest
    abseil::abseil
)

# Register the test with CTest
add_test(
    NAME tagger_test
    COMMAND tagger_test
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# Add tagging registry test executable
add_executable(tagging_registry_test
    test/tagging_registry_test.cc
)

target_link_libraries(tagging_registry_test
    tagging
    logger
    gtest::gtest
    abseil::abseil
    measurement_utils
    spectator
)

# Register the test with CTest
add_test(
    NAME tagging_registry_test
    COMMAND tagging_registry_test
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)