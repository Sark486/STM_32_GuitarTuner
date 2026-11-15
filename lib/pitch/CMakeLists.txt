# Pitch detector.
add_library(pitch STATIC
    src/pitch.c
)

target_include_directories(pitch
    PUBLIC  include
    PRIVATE src
)

target_link_libraries(pitch PRIVATE m tuner_warnings)
