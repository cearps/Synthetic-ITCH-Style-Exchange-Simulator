# ADR 0003: Event Log Persistence

## Status

Accepted

## Context

The event log is the source of truth for the exchange simulator. Currently, `DeterministicEventLog` stores events in memory only. For the system to be useful for backtesting, offline replay, verification, and long-running simulations, events must be persisted to durable storage.

Requirements:
- Events must be saved to disk (or other storage) for later replay
- Loading saved events must restore deterministic state (same seed = same events)
- Must support both incremental saves (during simulation) and full saves (at end)
- Storage format should be efficient (binary) and deterministic
- Should enable future storage backends (database, remote storage) without breaking existing code
- Must preserve the event log's role as authoritative source of truth

Constraints:
- Determinism is critical: same seed must produce identical events
- Performance matters: high-frequency event logging during simulation
- Windows primary dev environment
- Interface-based design must be maintained

## Decision

We will implement persistence using a **hybrid approach** that extends the `IEventLog` interface with persistence methods while using a pluggable storage abstraction layer.

### 1. Extend IEventLog Interface

Add persistence methods to `IEventLog`:

```cpp
class IEventLog {
    // ... existing methods ...
    
    // Persistence
    virtual bool save(const std::string& path) const = 0;
    virtual bool load(const std::string& path) = 0;
    virtual bool is_persistent() const = 0;
    virtual bool supports_incremental_save() const = 0;
};
```

### 2. Create Storage Abstraction Interface

Introduce `IEventLogStorage` interface for pluggable storage backends:

```cpp
class IEventLogStorage {
public:
    virtual ~IEventLogStorage() = default;
    
    virtual bool save(const EventLogSnapshot& snapshot, const std::string& path) = 0;
    virtual bool load(EventLogSnapshot& snapshot, const std::string& path) = 0;
    virtual bool append(const EventLogSnapshot& snapshot, const std::string& path) = 0;
    virtual bool supports_append() const = 0;
};
```

### 3. Initial Implementation: File-Based Storage

Implement `FileEventLogStorage` as the default storage backend:

- **Format**: Binary format with header containing:
  - Magic number (file format identifier)
  - Version number
  - Seed value
  - Event counts
  - Timestamp metadata
- **Structure**: 
  - Header (fixed size, deterministic encoding)
  - Event records (binary-encoded events in sequence)
- **Append support**: Append-only writes for incremental saves
- **Determinism**: Same events produce identical file content

### 4. Event Log Snapshot Structure

Create a snapshot structure to encapsulate all log state:

```cpp
struct EventLogSnapshot {
    uint64_t seed;
    uint64_t sequence_number;
    std::vector<OrderEvent> order_events;
    std::vector<TradeEvent> trade_events;
    std::vector<BookUpdateEvent> book_update_events;
};
```

## Alternatives considered

- **Option 1: Extend interface only (no storage abstraction)**
  - Rejected: Makes it harder to swap storage backends later. File I/O would be tightly coupled to `DeterministicEventLog`.

- **Option 2: Storage abstraction only (no interface extension)**
  - Rejected: Persistence becomes optional/invisible. Callers wouldn't know if a log implementation supports persistence.

- **Option 3: Separate persistent wrapper class**
  - Rejected: Adds complexity with wrapper pattern. Persistence should be a core capability of the log, not an add-on.

- **Option 4: JSON/text format**
  - Rejected: Less efficient, harder to ensure determinism (floating point representation, ordering). Binary format preferred for performance and determinism.

- **Option 5: Database storage (SQLite)**
  - Rejected for v0: Adds dependency, more complex. Can be added later via storage abstraction interface.

## Consequences

### Pros:
- **Flexibility**: Storage backend can be swapped via `IEventLogStorage` interface
- **Extensibility**: Easy to add database, remote storage, or other backends later
- **Determinism**: Binary format with deterministic encoding ensures same events = same file
- **Performance**: Binary format is efficient for high-frequency logging
- **Clear contract**: Interface clearly indicates persistence capabilities

### Cons:
- **Complexity**: Additional abstraction layer adds some complexity
- **File format versioning**: Need to handle format versioning as structure evolves
- **Initial overhead**: More code to write and maintain

### Implementation considerations:

1. **File format versioning**: Header must include version number for future migrations
2. **Error handling**: Save/load operations must handle file I/O errors gracefully
3. **Atomic writes**: Incremental appends should be atomic to avoid corruption
4. **Compression**: May want optional compression for large logs (future enhancement)
5. **Checksums**: Consider adding checksums to verify file integrity

### Follow-on work:

1. Implement `IEventLogStorage` interface
2. Implement `FileEventLogStorage` with binary format
3. Implement `EventLogSnapshot` structure
4. Extend `IEventLog` interface with persistence methods
5. Implement persistence in `DeterministicEventLog`
6. Add unit tests for save/load round-trips
7. Add determinism tests (same seed + events = same file)
8. Document file format specification

## Notes / References

- Event log as source of truth (from project goal)
- ADR 0002: Pipeline Architecture (interface-based design)
- Determinism requirement (identical seeds produce identical results)

