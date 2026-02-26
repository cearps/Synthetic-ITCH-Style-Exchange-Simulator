cmake_minimum_required(VERSION 3.15)
project(qrsdp_simulator VERSION 0.2.0 LANGUAGES CXX C)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Build type defaults to Debug if not specified
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

# Compiler warnings — applied per-target to avoid polluting third-party deps
if(MSVC)
    set(PROJECT_WARNING_FLAGS /W4 /WX)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
else()
    set(PROJECT_WARNING_FLAGS -Wall -Wextra -Wpedantic -Werror)
endif()

# Include directories
include_directories(src)

# --- Third-party: LZ4 (vendored, BSD licence) ---
add_library(lz4 STATIC third_party/lz4/lz4.c)
target_include_directories(lz4 PUBLIC third_party/lz4)
set_target_properties(lz4 PROPERTIES
    C_STANDARD 99
    POSITION_INDEPENDENT_CODE ON
)

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
set(IO_SOURCES
    src/io/in_memory_sink.cpp
    src/io/binary_file_sink.cpp
    src/io/event_log_reader.cpp
)

# --- ITCH 5.0 encoding, MoldUDP64, and UDP sender (no external deps) ---
set(ITCH_SOURCES
    src/itch/itch_encoder.cpp
    src/itch/moldudp64.cpp
    src/itch/udp_sender.cpp
)

# --- Optional Kafka support (Docker-only by default) ---
option(BUILD_KAFKA_SUPPORT "Enable Kafka sink (requires librdkafka)" OFF)
if(BUILD_KAFKA_SUPPORT)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(RDKAFKA REQUIRED IMPORTED_TARGET rdkafka++)
    list(APPEND IO_SOURCES src/io/kafka_sink.cpp)
    list(APPEND ITCH_SOURCES src/itch/itch_stream_consumer.cpp)
endif()
set(PRODUCER_SOURCES
    src/producer/qrsdp_producer.cpp
    src/producer/session_runner.cpp
)

set(LIBRARY_SOURCES
    ${BOOK_SOURCES}
    ${MODEL_SOURCES}
    ${CALIBRATION_SOURCES}
    ${SAMPLER_SOURCES}
    ${RNG_SOURCES}
    ${IO_SOURCES}
    ${ITCH_SOURCES}
    ${PRODUCER_SOURCES}
)

# Static library shared by CLI, tests, and UI
add_library(simulator_lib STATIC ${LIBRARY_SOURCES})
target_compile_options(simulator_lib PRIVATE ${PROJECT_WARNING_FLAGS})
target_link_libraries(simulator_lib PUBLIC lz4)

if(BUILD_KAFKA_SUPPORT)
    target_link_libraries(simulator_lib PUBLIC PkgConfig::RDKAFKA)
    target_compile_definitions(simulator_lib PUBLIC QRSDP_KAFKA_ENABLED)
endif()

# Headless CLI executable
add_executable(qrsdp_cli src/main.cpp)
target_compile_options(qrsdp_cli PRIVATE ${PROJECT_WARNING_FLAGS})
target_link_libraries(qrsdp_cli PRIVATE simulator_lib)

# Log inspection tool
add_executable(qrsdp_log_info src/log_info_main.cpp)
target_compile_options(qrsdp_log_info PRIVATE ${PROJECT_WARNING_FLAGS})
target_link_libraries(qrsdp_log_info PRIVATE simulator_lib)

# Multi-day session runner
add_executable(qrsdp_run src/run_main.cpp)
target_compile_options(qrsdp_run PRIVATE ${PROJECT_WARNING_FLAGS})
target_link_libraries(qrsdp_run PRIVATE simulator_lib)

# HLR calibration tool
add_executable(qrsdp_calibrate src/calibrate_main.cpp)
target_compile_options(qrsdp_calibrate PRIVATE ${PROJECT_WARNING_FLAGS})
target_link_libraries(qrsdp_calibrate PRIVATE simulator_lib)

# ITCH stream consumer (Kafka -> ITCH 5.0 over UDP multicast)
add_executable(qrsdp_itch_stream src/itch_stream_main.cpp)
target_compile_options(qrsdp_itch_stream PRIVATE ${PROJECT_WARNING_FLAGS})
target_link_libraries(qrsdp_itch_stream PRIVATE simulator_lib)

# Reference ITCH listener (joins multicast, decodes, prints)
add_executable(qrsdp_listen src/listen_main.cpp)
target_compile_options(qrsdp_listen PRIVATE ${PROJECT_WARNING_FLAGS})
target_link_libraries(qrsdp_listen PRIVATE simulator_lib)

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
        # core
        tests/core/test_records.cpp
        tests/core/test_interfaces.cpp
        # io
        tests/io/test_binary_file_sink.cpp
        tests/io/test_event_log_reader.cpp
        tests/io/test_multiplex_sink.cpp
        # book
        tests/book/test_book.cpp
        # model
        tests/model/test_intensity.cpp
        tests/model/test_curve_intensity.cpp
        # calibration
        tests/calibration/test_calibration.cpp
        # sampler
        tests/sampler/test_sampler.cpp
        tests/sampler/test_attribute_sampler.cpp
        # producer
        tests/producer/test_producer.cpp
        tests/producer/test_session_runner.cpp
        # itch
        tests/itch/test_itch_encoder.cpp
        tests/itch/test_moldudp64.cpp
        tests/itch/test_udp_roundtrip.cpp
        tests/itch/test_e2e_pipeline.cpp
    )

    if(TEST_SOURCES)
        add_executable(tests ${TEST_SOURCES})
        target_compile_options(tests PRIVATE ${PROJECT_WARNING_FLAGS})
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
    target_link_libraries(qrsdp_run PRIVATE ws2_32)
    target_link_libraries(qrsdp_calibrate PRIVATE ws2_32)
    target_link_libraries(qrsdp_itch_stream PRIVATE ws2_32)
    target_link_libraries(qrsdp_listen PRIVATE ws2_32)
    if(BUILD_TESTING AND TEST_SOURCES)
        target_link_libraries(tests PRIVATE ws2_32)
    endif()
else()
    target_link_libraries(qrsdp_cli PRIVATE pthread)
    target_link_libraries(qrsdp_run PRIVATE pthread)
    target_link_libraries(qrsdp_calibrate PRIVATE pthread)
    target_link_libraries(qrsdp_itch_stream PRIVATE pthread)
    target_link_libraries(qrsdp_listen PRIVATE pthread)
    if(BUILD_TESTING AND TEST_SOURCES)
        target_link_libraries(tests PRIVATE pthread)
    endif()
endif()

# Install rules
install(TARGETS qrsdp_cli
    RUNTIME DESTINATION bin
)
