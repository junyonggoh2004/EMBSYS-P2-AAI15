# tflmicro.cmake — Wrapper to build the pico-tflmicro static library
# from the git submodule without using the upstream CMakeLists.txt
# (which conflicts with our project() and pico_sdk_init() calls).

set(TFLM_DIR ${CMAKE_CURRENT_LIST_DIR}/project_v3/third_party/pico-tflmicro)
set(TFLM_SRC ${TFLM_DIR}/src)

# ---- Collect all .cpp and .c source files from the library ----
file(GLOB_RECURSE TFLM_SRCS
    "${TFLM_SRC}/signal/*.cpp"
    "${TFLM_SRC}/signal/*.c"
    "${TFLM_SRC}/tensorflow/*.cpp"
    "${TFLM_SRC}/tensorflow/*.c"
    "${TFLM_SRC}/third_party/cmsis_nn/Source/*.c"
    "${TFLM_SRC}/third_party/kissfft/*.c"
)

# ---- Exclude test files, examples, and benchmarks ----
list(FILTER TFLM_SRCS EXCLUDE REGEX ".*_test\\.cpp$")
list(FILTER TFLM_SRCS EXCLUDE REGEX ".*test_helpers\\.cpp$")
list(FILTER TFLM_SRCS EXCLUDE REGEX ".*test_helper_custom_ops\\.cpp$")
list(FILTER TFLM_SRCS EXCLUDE REGEX ".*fake_micro_context\\.cpp$")
list(FILTER TFLM_SRCS EXCLUDE REGEX ".*mock_micro_graph\\.cpp$")
list(FILTER TFLM_SRCS EXCLUDE REGEX ".*recording_micro_allocator\\.cpp$")
list(FILTER TFLM_SRCS EXCLUDE REGEX ".*hexdump_test\\.cpp$")
list(FILTER TFLM_SRCS EXCLUDE REGEX ".*/benchmarks/.*")
list(FILTER TFLM_SRCS EXCLUDE REGEX ".*/examples/.*")
list(FILTER TFLM_SRCS EXCLUDE REGEX ".*/tests/.*")

# ---- Define the static library ----
add_library(pico-tflmicro STATIC ${TFLM_SRCS})

target_include_directories(pico-tflmicro
    PUBLIC
    ${TFLM_SRC}/
    ${TFLM_SRC}/third_party/ruy
    ${TFLM_SRC}/third_party/gemmlowp
    ${TFLM_SRC}/third_party/kissfft
    ${TFLM_SRC}/third_party/flatbuffers
    ${TFLM_SRC}/third_party/cmsis/CMSIS/Core/Include
    ${TFLM_SRC}/third_party/flatbuffers/include
    ${TFLM_SRC}/third_party/cmsis_nn/Include
)

target_compile_definitions(pico-tflmicro
    PUBLIC
    TF_LITE_DISABLE_X86_NEON=1
    TF_LITE_STATIC_MEMORY=1
    TF_LITE_USE_CTIME=1
    CMSIS_NN=1
    ARDUINO=1
    TFLITE_USE_CTIME=1
)

target_compile_options(pico-tflmicro
    PRIVATE
    -Os
    -fno-rtti
    -fno-exceptions
    -fno-threadsafe-statics
)

target_link_libraries(pico-tflmicro
    pico_stdlib
    pico_multicore
)
