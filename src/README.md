# Source Code Structure

This directory contains the C++ source code for the Synthetic ITCH Exchange Simulator.

## Overview

The codebase is organized into logical modules, each with clear interfaces to enable future component swapping while maintaining a monolithic structure for simplicity.

## Directory Structure

- **`core/`**: Core data types and fundamental structures (events, orders)
- **`producer/`**: Event generation components
- **`matching/`**: Order book and matching engine
- **`logging/`**: Event logging and replay functionality
- **`encoding/`**: ITCH binary message encoding/decoding
- **`streaming/`**: UDP network streaming
- **`exchange_simulator.h`**: Main orchestrator class

## Building

Implementation files (`.cpp`) will be added as development progresses. The current structure provides header files defining interfaces and class skeletons.

## Dependencies

- C++ standard library (C++17)
- Platform-specific socket libraries for UDP streaming (to be implemented)

## Implementation Status

Currently, only header files with class definitions are provided. Methods are declared but not yet implemented. This provides the architectural skeleton for implementation.

See [architecture documentation](../../docs/architecture.md) for detailed component descriptions.

