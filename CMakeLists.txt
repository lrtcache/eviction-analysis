cmake_minimum_required(VERSION 3.20)

project(
    bufpool_eviction
    VERSION 0.1.0
    DESCRIPTION "Buffer pool eviction algorithm experiments"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(CTest)
option(BUFPOOL_FETCH_GTEST "Download GoogleTest if it is not already installed" ON)

if(EXISTS "${PROJECT_SOURCE_DIR}/src/CMakeLists.txt")
    add_subdirectory(src)
endif()

if(BUILD_TESTING AND EXISTS "${PROJECT_SOURCE_DIR}/tests/CMakeLists.txt")
    find_package(GTest CONFIG QUIET)

    if(NOT GTest_FOUND AND BUFPOOL_FETCH_GTEST)
        include(FetchContent)

        FetchContent_Declare(
            googletest
            URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.zip
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        )

        if(MSVC)
            set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
        endif()

        FetchContent_MakeAvailable(googletest)
    endif()

    add_subdirectory(tests)
endif()
