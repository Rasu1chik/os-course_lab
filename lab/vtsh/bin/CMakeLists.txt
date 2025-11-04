add_executable(
    vtsh
    main.c
)

target_include_directories(
    vtsh
    PUBLIC
    .
)

target_link_libraries(
    vtsh
    PRIVATE
    libvtsh
)


add_executable(
    cpu-factorize
    cpu-factorize.c
)


target_include_directories(
    cpu-factorize
    PUBLIC
    ${CMAKE_SOURCE_DIR}/lib
)

target_link_libraries(
    cpu-factorize
    PRIVATE
    m
)
