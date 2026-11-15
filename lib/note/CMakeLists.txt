add_library(note STATIC
    src/note.c
)

target_include_directories(note PUBLIC include)

target_compile_options(note PRIVATE ${TUNER_LIB_WARNINGS})

target_link_libraries(note PRIVATE m)