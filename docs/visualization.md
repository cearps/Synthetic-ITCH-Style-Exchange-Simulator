# Event Log Visualization

The simulator provides built-in visualization of the event log to help inspect and understand the generated market data.

## Quick Start

After running a simulation, the event log summary is automatically printed to the console. Simply run:

```bash
# Using Docker
docker-compose -f docker/docker-compose.yml run --rm simulator

# Or build and run locally
./build/itch_simulator
```

## Visualization Output

The visualization includes:

### 1. Summary Statistics

- **Seed**: The random seed used for the simulation
- **Total Sequence Number**: Total number of events logged
- **Event Counts**: Breakdown of order events, trade events, and book update events

### 2. Order Event Breakdown

- Count of `ADD` orders (limit orders added to book)
- Count of `CANCEL` orders (orders cancelled)
- Count of `AGGRESSIVE` orders (market orders that immediately match)

### 3. Trade Statistics

- **Total Volume**: Sum of all traded quantities
- **Total Notional**: Sum of (price × quantity) for all trades
- **Average Price**: Volume-weighted average execution price

### 4. Event Timeline

- **First 20 Order Events**: Table showing sequence number, type, side, price, quantity, and timestamp
- **First 10 Trade Events**: Table showing buy/sell order IDs, execution price, quantity, and timestamp

### 5. Price Over Time Chart

- **ASCII Chart**: Visual representation of price movement over the last 50 trades
- **Y-axis**: Price in ticks (scaled to show price range)
- **X-axis**: Time (trade sequence)
- **Connected Line**: Price points are connected with lines to show price evolution
- **Price Range**: Shows min/max prices and the price range in ticks

### 6. CSV Export

After each simulation run, all trade data is automatically exported to a CSV file in the `data/` directory. The filename format is `price_data_seed_<seed>.csv`.

**CSV Format:**
- `timestamp_ns`: Nanoseconds since epoch for the trade
- `sequence_number`: Event sequence number
- `price`: Execution price (in ticks)
- `quantity`: Execution quantity (in shares)
- `buy_order_id`: ID of the buy order
- `sell_order_id`: ID of the sell order

**Usage:**
The CSV file can be opened in Excel, Python (pandas/matplotlib), R, or any data visualization tool for better analysis and charting than the terminal ASCII chart.

## Example Output

```
======================================================================
                    EVENT LOG SUMMARY
======================================================================

Configuration:
  Seed: 12345
  Total Sequence Number: 150

Event Counts:
  Order Events: 100
  Trade Events: 30
  Book Update Events: 20

Order Event Breakdown:
  ADDs: 60
  CANCELs: 25
  AGGRESSIVE: 15

Trade Statistics:
  Total Volume: 5000
  Total Notional: 500000.00
  Average Price: 100.00

----------------------------------------------------------------------
First 20 Order Events:
----------------------------------------------------------------------
Seq     Type         Side     Price      Qty        Timestamp (ns)
----------------------------------------------------------------------
1       ADD          BUY      10000      100        0
2       ADD          SELL     10100      200        1000000
3       AGGRESSIVE   BUY      0          150        2000000
...

----------------------------------------------------------------------
Price Over Time (Last 50 Trades):
----------------------------------------------------------------------
Price
 10107 | *** *****  *  ***                 * *       **  *
 10102 |.  ..    . .. .  .                ....      . . .
  ...
  9997 |*   *     ** **   ***************** * *******  **
       +------------------------------------------------------------
        Time (trade sequence)

Price Range: 9997 - 10107 (range: 110 ticks)
```

## Programmatic Access

If you need to access the event log programmatically (e.g., for custom visualization or analysis), you can:

1. **Access the event log from the simulator**:

   ```cpp
   auto* log = dynamic_cast<DeterministicEventLog*>(simulator.get_event_log());
   const auto& order_events = log->get_order_events();
   const auto& trade_events = log->get_trade_events();
   const auto& book_updates = log->get_book_update_events();
   ```

2. **Use the accessor methods**:
   - `get_order_events()`: Returns all order events (ADD, CANCEL, AGGRESSIVE)
   - `get_trade_events()`: Returns all trade/execution events
   - `get_book_update_events()`: Returns all book update events

## CSV Export Details

The CSV export feature automatically creates a file after each simulation run. To use it:

1. **Ensure the data directory exists**: The simulator will create `data/price_data_seed_<seed>.csv`
2. **Mount the data directory when using Docker**: Use `-v "${PWD}/data:/app/data"` to access the CSV file on your host machine
3. **Visualize in your preferred tool**: Import the CSV into Excel, Python, R, or any visualization tool

**Example:**
```bash
# Run simulation with CSV export
docker run --rm -v "${PWD}/data:/app/data" docker-simulator /app/build/itch_simulator 99999

# The CSV file will be at: data/price_data_seed_99999.csv
```

## Python Visualization Tool

A Python script is provided to create comprehensive visualizations of the price data:

### Setup

Install required Python packages:

```bash
pip install -r tools/requirements.txt
```

### Usage

```bash
# Use the most recent CSV file in data/
python tools/visualize_price_data.py

# Or specify a specific CSV file
python tools/visualize_price_data.py data/price_data_seed_99999.csv
```

### Output

The script generates two visualization files in the `data/` directory:

1. **`price_visualization.png`**: Comprehensive dashboard with 6 charts:
   - Price over time (main chart)
   - Price distribution histogram
   - Trading volume over time
   - Price change distribution
   - Volume-weighted average price (VWAP)
   - Summary statistics

2. **`price_simple.png`**: Simple price chart with filled area

The script also prints detailed summary statistics to the console, including:
- Total trades and time range
- Price statistics (min, max, average, range, std dev)
- Volume statistics
- Price change statistics

## Future Enhancements

Potential future visualization improvements:

- JSON export for structured data analysis
- Interactive web-based viewer
- Order book depth visualization
- Statistical analysis (spread distribution, volume profile)
- Export to database for querying
