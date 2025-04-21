add_library(aws INTERFACE
    src/aws.h
)

target_include_directories(aws
    INTERFACE ${CMAKE_SOURCE_DIR}
)

# Add dependencies
target_link_libraries(aws
    INTERFACE
    fmt::fmt
    rapidjson
)