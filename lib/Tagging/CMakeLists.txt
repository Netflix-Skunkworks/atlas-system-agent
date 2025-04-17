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