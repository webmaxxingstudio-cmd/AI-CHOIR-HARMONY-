cmake_minimum_required(VERSION 3.22)
project(VocalChoirStudio VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(VCS_BUILD_PLUGIN "Build the JUCE plugin" ON)
option(VCS_BUILD_TESTS "Build the core DSP test suite" ON)
set(VCS_JUCE_DIR "" CACHE PATH "Path to a local JUCE checkout (fetched automatically if empty)")

# Core DSP engine: plain C++, no JUCE dependency, unit tested on its own.
add_subdirectory(core)

if(VCS_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(VCS_BUILD_PLUGIN)
    if(VCS_JUCE_DIR)
        add_subdirectory(${VCS_JUCE_DIR} ${CMAKE_BINARY_DIR}/JUCE EXCLUDE_FROM_ALL)
    else()
        include(FetchContent)
        FetchContent_Declare(JUCE
            GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
            GIT_TAG 8.0.9
            GIT_SHALLOW ON)
        FetchContent_MakeAvailable(JUCE)
    endif()
    add_subdirectory(plugin)
endif()
