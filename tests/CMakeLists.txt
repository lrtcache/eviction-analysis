add_executable(bufpool_eviction_tests
    runner_test.cc
    bufpool_test.cc
    config_test.cc
)

target_include_directories(bufpool_eviction_tests
    PRIVATE
        ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(bufpool_eviction_tests
    PRIVATE
        GTest::gtest_main
)

if(MSVC)
    target_compile_options(bufpool_eviction_tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(bufpool_eviction_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

include(GoogleTest)
gtest_discover_tests(bufpool_eviction_tests)
