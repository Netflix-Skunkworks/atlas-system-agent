#-- contain library
add_library(contain
    "contain.c"
    "contain.h"
)
target_link_libraries(contain cap)
