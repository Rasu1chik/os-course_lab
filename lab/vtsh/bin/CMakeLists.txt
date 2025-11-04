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

add_executable(
    ema-traverse-graph
    ema-traverse-graph.c
)

target_include_directories(
    ema-traverse-graph
    PUBLIC
    ${CMAKE_SOURCE_DIR}/lib
)

target_link_libraries(
    ema-traverse-graph
    PRIVATE
    m
)

