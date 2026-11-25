add_executable(vcs_tests
    test_main.cpp
    test_core_dsp.cpp
    test_harmony_choir.cpp
)
target_link_libraries(vcs_tests PRIVATE vcs_core)
add_test(NAME vcs_tests COMMAND vcs_tests)
