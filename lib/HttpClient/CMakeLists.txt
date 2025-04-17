add_library(HttpClient
    src/http_client.cpp
    src/http_client.h
    src/http_client_instantiations.cpp
    src/log_entry.h
)

# Depend on the custom target that ensures valid_chars.inc is generated
add_dependencies(HttpClient generate_valid_chars)

target_include_directories(HttpClient
    PUBLIC ${CMAKE_SOURCE_DIR}
)

# Add dependencies based on what I see in the codebase
target_link_libraries(HttpClient
    PUBLIC
    abseil::abseil
    fmt::fmt
    CURL::libcurl
)