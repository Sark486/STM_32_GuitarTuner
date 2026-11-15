add_library(note STATIC
    src/note.c
)

target_include_directories(note PUBLIC include)

target_link_libraries(note PRIVATE m tuner_warnings)
