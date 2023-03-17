#-- contain library
add_library(contain
    "contain.c"
    "contain.h"
)
target_include_directories(contain PRIVATE ${CMAKE_SOURCE_DIR})
# linkopts = ["-lcap"]
target_link_libraries(contain cap ${CONAN_LIBS})
