add_library(
    vtpc
    SHARED
    vtpc.c
)

target_include_directories(
    vtpc
    PUBLIC
    .
)
target_compile_definitions(vtpc PUBLIC
    VTPC_BLOCK_SIZE=4096
    VTPC_CACHE_PAGES=64
    VTPC_MAX_OPEN_FILES=32
)