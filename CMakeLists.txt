cmake_minimum_required(VERSION 3.15)
project(itch_simulator VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Build type defaults to Debug if not specified
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

# Compiler warnings
if(MSVC)
    add_compile_options(/W4 /WX-)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Werror)
endif()

# Include directories
include_directories(src)

# Source files
set(CORE_SOURCES
    src/core/events.cpp
    src/core/order.cpp
)

set(PRODUCER_SOURCES
    src/producer/event_producer.cpp
)

set(MATCHING_SOURCES
    src/matching/order_book.cpp
    src/matching/matching_engine.cpp
)

set(LOGGING_SOURCES
    src/logging/event_log.cpp
)

set(ENCODING_SOURCES
    src/encoding/itch_encoder.cpp
)

set(STREAMING_SOURCES
    src/streaming/udp_streamer.cpp
)

set(SIMULATOR_SOURCES
    src/exchange_simulator.cpp
    src/main.cpp
)

# Combine all sources
set(ALL_SOURCES
    ${CORE_SOURCES}
    ${PRODUCER_SOURCES}
    ${MATCHING_SOURCES}
    ${LOGGING_SOURCES}
    ${ENCODING_SOURCES}
    ${STREAMING_SOURCES}
    ${SIMULATOR_SOURCES}
)

# Main executable (exclude main.cpp from ALL_SOURCES since it's in SIMULATOR_SOURCES)
set(LIBRARY_SOURCES
    ${CORE_SOURCES}
    ${PRODUCER_SOURCES}
    ${MATCHING_SOURCES}
    ${LOGGING_SOURCES}
    ${ENCODING_SOURCES}
    ${STREAMING_SOURCES}
    src/exchange_simulator.cpp
)

# Main executable
add_executable(itch_simulator ${SIMULATOR_SOURCES} ${LIBRARY_SOURCES})

# Google Test setup
option(BUILD_TESTING ON)
if(BUILD_TESTING)
    enable_testing()
    
    # Download Google Test
    include(FetchContent)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        v1.14.0
    )
    FetchContent_MakeAvailable(googletest)
    
    # Test sources
    set(TEST_SOURCES
        tests/core/test_events.cpp
        tests/core/test_order.cpp
        # Add more test files here as they are created
    )
    
    if(TEST_SOURCES)
        add_executable(tests ${TEST_SOURCES} ${LIBRARY_SOURCES})
        target_link_libraries(tests 
            PRIVATE 
            gtest_main
        )
        include(GoogleTest)
        gtest_discover_tests(tests)
    endif()
endif()

# Platform-specific libraries
if(WIN32)
    target_link_libraries(itch_simulator PRIVATE ws2_32)
    if(BUILD_TESTING AND TEST_SOURCES)
        target_link_libraries(tests PRIVATE ws2_32)
    endif()
else()
    target_link_libraries(itch_simulator PRIVATE pthread)
    if(BUILD_TESTING AND TEST_SOURCES)
        target_link_libraries(tests PRIVATE pthread)
    endif()
endif()

# Install rules (optional, for packaging)
install(TARGETS itch_simulator
    RUNTIME DESTINATION bin
)

