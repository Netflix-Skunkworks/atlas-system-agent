add_library(ebs
    src/ebs.cpp
    src/ebs.h
)

target_include_directories(ebs
    PUBLIC ${CMAKE_SOURCE_DIR}
)

# Add dependencies
target_link_libraries(ebs
    fmt::fmt
    abseil::abseil
    spectator
    tagging
)

# add_executable(ebs_test
#     test/ebs_test.cpp
# )

# target_link_libraries(ebs_test
#     ebs
#     util
#     logger
#     measurement_utils
#     gtest::gtest
# )

# # Register the test with CTest
# add_test(
#     NAME ebs_test
#     COMMAND ebs_test
#     WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
# )