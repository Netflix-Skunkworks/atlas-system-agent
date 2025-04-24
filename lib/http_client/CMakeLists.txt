add_library(HttpClient
    src/http_client.cpp
    src/http_client.h
    src/log_entry.h
)

target_include_directories(HttpClient
    PUBLIC ${CMAKE_SOURCE_DIR}
)

# Add dependencies based on what I see in the codebase
target_link_libraries(HttpClient
    PUBLIC
    abseil::abseil
    fmt::fmt
    CURL::libcurl
    spectator
    tagging
)