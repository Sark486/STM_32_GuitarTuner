# Portable C modules, shared by two builds:
#   - the firmware (root CMakeLists.txt, arm-none-eabi-gcc), and
#   - the host tests (tests/CMakeLists.txt, the PC's own compiler).

# One warning set for the modules, the firmware and the tests: link tuner_warnings PRIVATE to get it.
add_library(tuner_warnings INTERFACE)
target_compile_options(tuner_warnings INTERFACE
    -Wall
    -Wextra
    -Wdouble-promotion   # float silently promoted to double (slow on the M4F)
    -Wfloat-conversion   # double -> float, float -> int without a cast
    -Wconversion         # any implicit conversion that can change a value
    -Wshadow             # a local hiding another variable
    -Wundef              # an undefined macro used in #if
)

add_subdirectory(level)
add_subdirectory(pitch)
add_subdirectory(note)
