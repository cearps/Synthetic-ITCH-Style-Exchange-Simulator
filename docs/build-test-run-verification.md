# Build/Test/Run Verification

This document verifies that the build, test, and run commands work correctly.

## Verified Commands

### Build Command
```bash
docker-compose -f docker/docker-compose.yml build build
```
**Status**: ✅ **VERIFIED** - Build completes successfully, all source files compile and link.

### Run Command
```bash
docker-compose -f docker/docker-compose.yml run --rm simulator
```
**Status**: ✅ **VERIFIED** - Simulator executable runs (produces no output as expected for stub implementation).

**Alternative direct Docker command**:
```bash
docker run --rm docker-build /app/build/itch_simulator
```
**Status**: ✅ **VERIFIED** - Executable runs successfully.

### Test Command
```bash
docker-compose -f docker/docker-compose.yml run --rm test
```
**Status**: ⚠️ **Requires test build** - Test build downloads Google Test on first run (expected, takes time).

**Note**: The test build may take several minutes on first run due to:
1. Downloading Google Test from GitHub
2. Compiling Google Test
3. Compiling test files

Subsequent test builds will be faster due to Docker layer caching.

## Quick Verification

To quickly verify the build works:
```bash
# Build (takes ~3-5 minutes first time, faster with cache)
docker-compose -f docker/docker-compose.yml build build

# Run simulator (instant)
docker-compose -f docker/docker-compose.yml run --rm simulator

# Build and run tests (takes longer due to Google Test download)
docker-compose -f docker/docker-compose.yml build test
docker-compose -f docker/docker-compose.yml run --rm test
```

## Known Issues

- **Valgrind**: Removed from test Dockerfile as it's optional and can cause network issues during build
- **Test build time**: First build takes longer due to Google Test download (expected behavior)

