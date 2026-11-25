add_library(vcs_core STATIC
    src/Fft.cpp
    src/YinPitchDetector.cpp
    src/MusicTheory.cpp
    src/VocalAnalyzer.cpp
    src/PsolaVoice.cpp
    src/ChoirEngine.cpp
)
target_include_directories(vcs_core PUBLIC include)
target_compile_definitions(vcs_core PUBLIC _USE_MATH_DEFINES)
if(MSVC)
    target_compile_options(vcs_core PRIVATE /W4 /fp:fast)
else()
    target_compile_options(vcs_core PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()
