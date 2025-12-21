# Build, Test, and Run Plan

This document outlines the build system, testing framework, and run procedures for the Synthetic ITCH Exchange Simulator. It is designed to be followed by both humans and AI coding agents.

## Overview

The project uses:
- **CMake** for C++ builds (cross-platform)
- **Docker** for containerized builds and tests (ensures consistency)
- **Google Test** (gtest) for unit testing
- **Windows** as primary development environment, with Linux support via Docker

## Build System

### Prerequisites

#### Windows Native Build
- CMake 3.15 or later
- C++ compiler with C++17 support (MSVC 2017+, GCC 7+, or Clang 5+)
- Git

#### Docker Build (Recommended for Consistency)
- Docker Desktop (Windows)
- Docker Compose (usually included)

### Building the Project

#### Option 1: Docker Build (Recommended)

```bash
# Build the project in Docker
docker-compose build

# Or build manually
docker build -t itch-simulator -f docker/Dockerfile.build .
```

#### Option 2: Native Build on Windows

```bash
# Create build directory
mkdir build
cd build

# Configure with CMake
cmake ..

# Build
cmake --build . --config Release

# Or use CMake GUI on Windows
```

### Build Targets

- `itch_simulator`: Main executable
- `tests`: Test suite executable
- `all`: Build everything (default)

### Build Configuration

- `Release`: Optimized build (default for production)
- `Debug`: Debug symbols, no optimization (default for development)
- `RelWithDebInfo`: Optimized with debug symbols

Configure with:
```bash
cmake -DCMAKE_BUILD_TYPE=Release ..
```

## Testing

### Test Framework

We use **Google Test** (gtest) for unit testing. Tests are organized to mirror the source code structure:

```
src/
├── core/
│   └── (source files)
├── producer/
│   └── (source files)
└── ...

tests/
├── core/
│   └── test_events.cpp
│   └── test_order.cpp
├── producer/
│   └── test_event_producer.cpp
└── ...
```

### Running Tests

#### Docker (Recommended)

**First time setup:**
```bash
# Build the test image (downloads Google Test - may take several minutes)
docker-compose -f docker/docker-compose.yml build test
```

**Running tests:**
```bash
# Run all tests
docker-compose -f docker/docker-compose.yml run --rm test

# List available tests
docker-compose -f docker/docker-compose.yml run --rm test /app/build/tests --gtest_list_tests

# Run specific test by name
docker-compose -f docker/docker-compose.yml run --rm test /app/build/tests --gtest_filter=EventsTest.*
```

**Note:** The first build will be slower as it downloads and compiles Google Test. Subsequent builds use Docker layer caching and are much faster.

#### Native
```bash
cd build
ctest --output-on-failure

# Or run test executable directly
./tests
```

### Test Requirements for AI Agents

When writing tests, follow these rules:

1. **TDD Principles**: Write tests before or alongside implementation
2. **Test Structure**: Match source directory structure in `tests/`
3. **Test Naming**: Use descriptive names like `TestOrderCreation`, `TestMatchingEnginePriceTimePriority`
4. **Determinism**: All tests must be deterministic (no time-dependent or random behavior without seeds)
5. **Isolation**: Each test should be independent (no shared state)
6. **Coverage**: Aim for:
   - Unit tests for each class/component
   - Integration tests for component interactions
   - Determinism verification tests (same seed = same output)

### Test Categories

- **Unit Tests**: Test individual components in isolation
- **Integration Tests**: Test component interactions
- **Determinism Tests**: Verify same seed produces identical results
- **Replay Tests**: Verify event log replay produces identical behavior

### Example Test Structure

```cpp
#include <gtest/gtest.h>
#include "core/events.h"

namespace exchange {
namespace test {

TEST(OrderEventTest, CreateOrderEvent) {
    OrderEvent event{};
    event.type = EventType::ORDER_ADD;
    event.order_id = OrderId{1};
    event.symbol = Symbol{"AAPL"};
    event.side = OrderSide::BUY;
    event.price = Price{10000};
    event.quantity = Quantity{100};
    
    EXPECT_EQ(event.type, EventType::ORDER_ADD);
    EXPECT_EQ(event.order_id.value, 1);
}

} // namespace test
} // namespace exchange
```

## Running the Simulator

### Basic Run

```bash
# Docker
docker-compose -f docker/docker-compose.yml run --rm simulator

# Native
./build/itch_simulator
```

### Configuration

Configuration is provided via command-line arguments or configuration file:

```bash
# Example: Run with seed
./itch_simulator --seed 12345 --duration 3600

# Example: Enable UDP streaming
./itch_simulator --seed 12345 --udp-stream --host 127.0.0.1 --port 9999
```

### Run Modes

1. **Simulation Mode**: Generate events and stream them
2. **Replay Mode**: Replay events from an existing event log
3. **Test Mode**: Run with verification checks enabled

## CI/CD Considerations

### Docker-Based CI

Use Docker for consistent builds across environments:

```yaml
# Example GitHub Actions / GitLab CI
build:
  script:
    - docker-compose -f docker/docker-compose.yml build build
    - docker-compose -f docker/docker-compose.yml build test
    - docker-compose -f docker/docker-compose.yml run --rm test
```

### Build Artifacts

- Executables: `build/itch_simulator`, `build/tests`
- Test results: `build/Testing/Temporary/LastTest.log`
- Coverage reports: `build/coverage/` (if enabled)

## Development Workflow for AI Agents

### When Adding New Code

1. **Write Tests First** (TDD):
   - Create test file in `tests/` matching source structure
   - Write failing test
   - Implement code to pass test
   - Refactor

2. **Run Tests**:
   ```bash
   docker-compose -f docker/docker-compose.yml run --rm test
   ```

3. **Build Verification**:
   ```bash
   docker-compose build
   ```

4. **Documentation**:
   - Update relevant docs if public interface changes
   - Add to worklog if major change (see `docs/worklog/WORKLOG.md`)

### When Modifying Existing Code

1. **Run Existing Tests**: Ensure nothing breaks
2. **Add New Tests**: If behavior changes
3. **Update Documentation**: If interfaces change

### Code Quality Checks

- **Compilation**: Must compile without warnings (treat warnings as errors in CI)
- **Tests**: All tests must pass
- **Determinism**: Same seed must produce identical output
- **Memory**: No memory leaks (use sanitizers in debug builds)

## Troubleshooting

### Build Issues

- **CMake not found**: Install CMake and ensure it's in PATH
- **Compiler not found**: Install Visual Studio Build Tools or use Docker
- **Missing dependencies**: Use Docker which includes all dependencies

### Test Issues

- **Tests fail with different seeds**: Check for non-deterministic behavior
- **Tests timeout**: Check for infinite loops or blocking operations
- **Memory errors**: Run with address sanitizer: `cmake -DUSE_SANITIZERS=ON ..`

### Docker Issues

- **Docker not running**: Start Docker Desktop
- **Permission denied**: Ensure Docker daemon is running with proper permissions
- **Build cache issues**: Use `docker-compose build --no-cache`

## File Structure

```
.
├── CMakeLists.txt           # Main CMake configuration
├── docker/
│   ├── Dockerfile.build     # Build environment
│   ├── Dockerfile.test      # Test environment
│   └── docker-compose.yml   # Compose configuration
├── src/                     # Source code
├── tests/                   # Test code
├── build/                   # Build output (gitignored)
└── docs/
    └── build-test-run.md    # This file
```

## Next Steps

1. Implement CMakeLists.txt
2. Create Docker files
3. Set up Google Test framework
4. Create initial test structure
5. Add CI/CD configuration (optional)

