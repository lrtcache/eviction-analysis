add_executable(bufpool_eviction
    main.cc
)
add_executable(lruk_exp
    lruk_exp.cc
)

target_include_directories(bufpool_eviction
    PRIVATE
        ${PROJECT_SOURCE_DIR}/include
)

target_include_directories(lruk_exp
    PRIVATE
        ${PROJECT_SOURCE_DIR}/include
)

if(MSVC)
    target_compile_options(bufpool_eviction PRIVATE /W4 /permissive-)
    target_compile_options(lruk_exp  PRIVATE /W4 /permissive-)
else()
    target_compile_options(bufpool_eviction PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(lruk_exp PRIVATE /W4 /permissive-)
endif()
