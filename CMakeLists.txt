cmake_minimum_required(VERSION 3.15)
project(qrsdp_simulator VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Build type defaults to Debug if not specified
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

# Compiler warnings
if(MSVC)
    add_compile_options(/W4 /WX)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Werror)
endif()

# Include directories
include_directories(src)

# Source files — organised by subdirectory
set(BOOK_SOURCES      src/book/multi_level_book.cpp)
set(MODEL_SOURCES
    src/model/simple_imbalance_intensity.cpp
    src/model/curve_intensity_model.cpp
    src/model/hlr_params.cpp
    src/model/intensity_curve.cpp
)
set(CALIBRATION_SOURCES
    src/calibration/intensity_estimator.cpp
    src/calibration/intensity_curve_io.cpp
)
set(SAMPLER_SOURCES
    src/sampler/competing_intensity_sampler.cpp
    src/sampler/unit_size_attribute_sampler.cpp
)
set(RNG_SOURCES       src/rng/mt19937_rng.cpp)
set(IO_SOURCES        src/io/in_memory_sink.cpp)
set(PRODUCER_SOURCES  src/producer/qrsdp_producer.cpp)

set(LIBRARY_SOURCES
    ${BOOK_SOURCES}
    ${MODEL_SOURCES}
    ${CALIBRATION_SOURCES}
    ${SAMPLER_SOURCES}
    ${RNG_SOURCES}
    ${IO_SOURCES}
    ${PRODUCER_SOURCES}
)

# Static library shared by CLI, tests, and UI
add_library(simulator_lib STATIC ${LIBRARY_SOURCES})

# Headless CLI executable
add_executable(qrsdp_cli src/main.cpp)
target_link_libraries(qrsdp_cli PRIVATE simulator_lib)

# Google Test setup
option(BUILD_TESTING "Enable testing" ON)
if(BUILD_TESTING)
    enable_testing()

    include(FetchContent)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        v1.14.0
    )
    FetchContent_MakeAvailable(googletest)

    set(TEST_SOURCES
        tests/qrsdp/test_records.cpp
        tests/qrsdp/test_interfaces.cpp
        tests/qrsdp/test_book.cpp
        tests/qrsdp/test_intensity.cpp
        tests/qrsdp/test_curve_intensity.cpp
        tests/qrsdp/test_calibration.cpp
        tests/qrsdp/test_sampler.cpp
        tests/qrsdp/test_attribute_sampler.cpp
        tests/qrsdp/test_producer.cpp
    )

    if(TEST_SOURCES)
        add_executable(tests ${TEST_SOURCES})
        target_link_libraries(tests PRIVATE simulator_lib gtest_main)
        include(GoogleTest)
        gtest_discover_tests(tests)
    endif()
endif()

# Optional QRSDP debugging UI (ImGui + ImPlot + GLFW)
option(BUILD_QRSDP_UI "Build qrsdp_ui debugging tool" ON)
if(BUILD_QRSDP_UI AND EXISTS "${CMAKE_SOURCE_DIR}/tools/qrsdp_ui/CMakeLists.txt")
    add_subdirectory(tools/qrsdp_ui)
endif()

# Platform-specific libraries
if(WIN32)
    target_link_libraries(qrsdp_cli PRIVATE ws2_32)
    if(BUILD_TESTING AND TEST_SOURCES)
        target_link_libraries(tests PRIVATE ws2_32)
    endif()
else()
    target_link_libraries(qrsdp_cli PRIVATE pthread)
    if(BUILD_TESTING AND TEST_SOURCES)
        target_link_libraries(tests PRIVATE pthread)
    endif()
endif()

# Install rules
install(TARGETS qrsdp_cli
    RUNTIME DESTINATION bin
)
