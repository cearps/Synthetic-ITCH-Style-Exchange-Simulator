# ITCH Encoder Scope (ASX Specification)

This document defines the scope of the ITCH encoder implementation for the Synthetic ITCH Exchange Simulator, based on the [ASX Trade ITCH Message Specification](https://www.asxonline.com/content/dam/asxonline/public/documents/asx-trade-refresh-manuals/asx-trade-itch-message-specification.pdf).

## Overview

The ITCH encoder converts internal event log events into binary ITCH-format messages suitable for streaming over UDP. The encoder follows the ASX ITCH message specification to ensure compatibility with standard market data consumers.

## Design Principles

- **Wire-level fidelity**: Messages match ASX ITCH format exactly
- **Deterministic encoding**: Same events produce identical binary output
- **Minimal v0 scope**: Support only essential message types for order book reconstruction
- **Extensible**: Easy to add additional message types later

## ASX ITCH Message Structure

All ASX ITCH messages follow a common structure:

```
+------------------+
| Message Header   |  (2 bytes: message type + length)
+------------------+
| Message Body     |  (variable length, message-specific)
+------------------+
```

### Message Header Format

- **Message Type** (1 byte): Identifies the message type
- **Message Length** (1 byte): Length of the entire message (including header)

> **Note**: ASX ITCH uses 1-byte length field, limiting messages to 255 bytes maximum. This is sufficient for most order book messages.

## v0 Message Types (In Scope)

For the initial implementation, we will support the following ASX ITCH message types:

### 1. Add Order (No MPID) - Message Type 'A'

**Purpose**: Add a new limit order to the order book.

**Maps from**: `OrderEvent` with `type = ORDER_ADD`

**Message Fields**:
- Order reference number (8 bytes, uint64)
- Buy/Sell indicator (1 byte)
- Quantity (4 bytes, uint32)
- Stock code (6 bytes, ASCII, right-padded)
- Price (4 bytes, uint32, in price ticks)

**v0 Implementation Notes**:
- Order reference number maps to our `OrderId`
- Buy/Sell indicator: 'B' for buy, 'S' for sell
- Price stored as integer ticks (matches our `Price` structure)
- Stock code limited to 6 characters (ASX standard)

### 2. Order Cancel - Message Type 'X'

**Purpose**: Cancel a portion of an existing order.

**Maps from**: `OrderEvent` with `type = ORDER_CANCEL`

**Message Fields**:
- Order reference number (8 bytes, uint64)
- Canceled quantity (4 bytes, uint32)

**v0 Implementation Notes**:
- Partial cancels supported (quantity < original order quantity)
- Full cancels represented as cancel with quantity = remaining quantity
- Order reference must match an existing order

### 3. Order Delete - Message Type 'D'

**Purpose**: Delete an order completely from the book.

**Maps from**: `OrderEvent` with `type = ORDER_CANCEL` where quantity equals remaining quantity

**Message Fields**:
- Order reference number (8 bytes, uint64)

**v0 Implementation Notes**:
- Used when an order is fully canceled or executed
- Simpler than cancel with full quantity for consumers

### 4. Order Executed - Message Type 'E'

**Purpose**: Report execution of an order at a price level.

**Maps from**: `TradeEvent` (individual execution)

**Message Fields**:
- Order reference number (8 bytes, uint64)
- Executed quantity (4 bytes, uint32)
- Match number (8 bytes, uint64, unique execution identifier)

**v0 Implementation Notes**:
- One message per order side (buy and sell orders get separate messages)
- Match number provides unique identifier for the trade
- Used for partial fills

### 5. Order Executed with Price - Message Type 'C'

**Purpose**: Report execution with explicit price (used for price improvement or special cases).

**Maps from**: `TradeEvent` (when price differs from best bid/ask)

**Message Fields**:
- Order reference number (8 bytes, uint64)
- Executed quantity (4 bytes, uint32)
- Match number (8 bytes, uint64)
- Price (4 bytes, uint32, in price ticks)

**v0 Implementation Notes**:
- Used when execution price is not at the best bid/ask
- Price improvement scenarios
- Less common than 'E' message type

### 6. Trade - Message Type 'P'

**Purpose**: Report a completed trade (both sides of the transaction).

**Maps from**: `TradeEvent` (complete trade)

**Message Fields**:
- Order reference number (buy side) (8 bytes, uint64)
- Order reference number (sell side) (8 bytes, uint64)
- Quantity (4 bytes, uint32)
- Price (4 bytes, uint32, in price ticks)
- Match number (8 bytes, uint64)

**v0 Implementation Notes**:
- Represents a completed trade with both sides
- May be used instead of or in addition to 'E' messages
- Provides complete trade information in one message

## Event to Message Type Mapping

| Internal Event Type | ASX ITCH Message Type | Notes |
|---------------------|------------------------|-------|
| `ORDER_ADD` | 'A' (Add Order) | Limit orders only in v0 |
| `ORDER_CANCEL` (partial) | 'X' (Order Cancel) | Partial cancel |
| `ORDER_CANCEL` (full) | 'D' (Order Delete) | Full cancel/delete |
| `ORDER_AGGRESSIVE_TAKE` | 'E' or 'C' (Order Executed) | Market/crossing orders |
| `TRADE` | 'P' (Trade) | Completed trade |
| `ORDER_BOOK_UPDATE` | Not directly encoded | Derived from order messages |

## Out of Scope for v0

The following ASX ITCH message types are **not** included in v0 but may be added later:

### System Messages
- **System Event Message** ('S'): Trading session events (start, end, halt)
- **Stock Directory** ('R'): Security information
- **Stock Trading Action** ('H'): Trading status changes

### Advanced Order Types
- **Add Order with MPID** ('F'): Orders with market participant ID
- **Order Replace** ('U'): Modify existing order (price/quantity change)

### Market Data
- **Net Order Imbalance Indicator** ('I'): Auction imbalance information
- **Retail Price Improvement Indicator** ('N'): RPI information

### Regulatory
- **Reg SHO Restriction** ('Y'): Short sale restrictions
- **Market Participant Position** ('L'): MPID position updates

### Other
- **Cross Trade** ('Q'): Off-book trades
- **Broken Trade** ('B'): Trade cancellations

## Message Encoding Details

### Byte Order
- All multi-byte integers use **big-endian** (network byte order)
- This matches ASX ITCH specification

### Price Representation
- Prices stored as **integer ticks**
- Example: $10.50 with 1 cent tick size = 1050 ticks
- 4-byte unsigned integer (uint32) supports prices up to $42,949,672.95 with 1 cent ticks

### Quantity Representation
- Quantities stored as **shares** (integer)
- 4-byte unsigned integer (uint32) supports up to 4,294,967,295 shares

### Timestamp Handling
- ASX ITCH messages include timestamps in nanoseconds since epoch
- Our `Timestamp` structure already uses nanoseconds
- Timestamp field position varies by message type

### Sequence Numbers
- ASX ITCH messages are sequenced
- Our `sequence_number` field maps to ITCH sequence numbers
- Sequence numbers must be monotonically increasing

## Implementation Notes

### Message Header Encoding

```cpp
struct ITCHMessageHeader {
    uint8_t message_type;  // ASCII character (e.g., 'A', 'X', 'E')
    uint8_t message_length; // Total message length including header
};
```

### Example: Add Order Message

For an order add event:
- Message type: 'A' (0x41)
- Message length: 23 bytes (2 header + 21 body)
- Body: OrderId (8) + Side (1) + Quantity (4) + Symbol (6) + Price (4)

### Determinism Requirements

- Same input events must produce identical binary output
- Field ordering must be consistent
- Padding/alignment must be deterministic
- No variable-length fields in v0 (all fixed-size)

## Testing Strategy

### Unit Tests
- Encode/decode round-trip for each message type
- Verify byte-level output matches expected format
- Test edge cases (max values, zero quantities, etc.)

### Integration Tests
- Full event log → ITCH messages → decode → verify
- Sequence number continuity
- Message ordering preservation

### Golden Tests
- Capture known-good ITCH message outputs
- Verify deterministic encoding across runs
- Compare against ASX specification examples (if available)

## Future Enhancements

### v1+ Additions
- System event messages (session start/end)
- Order replace messages (modify existing orders)
- Additional trade message types
- Market participant identifiers

### Calibration Support
- Message rate limiting
- Timestamp precision options
- Custom field mappings

## References

- [ASX Trade ITCH Message Specification](https://www.asxonline.com/content/dam/asxonline/public/documents/asx-trade-refresh-manuals/asx-trade-itch-message-specification.pdf)
- ASX Trade Refresh Manuals: https://www.asxonline.com/asx-trade-refresh-manuals

## Implementation Status

The `ITCHEncoder` class implements the `IITCHEncoder` interface defined in `src/encoding/itch_encoder.h`. The v0 implementation should support the message types documented above.

See the source code in `src/encoding/itch_encoder.cpp` for the current implementation status.

