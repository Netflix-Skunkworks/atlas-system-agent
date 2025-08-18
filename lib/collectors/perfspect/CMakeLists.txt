add_library(perfspect
    src/perfspect.cpp
    src/perfspect.h
)

target_include_directories(perfspect
    PUBLIC ${CMAKE_SOURCE_DIR}
)