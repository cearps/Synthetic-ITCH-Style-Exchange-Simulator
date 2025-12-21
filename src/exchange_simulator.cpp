#include "exchange_simulator.h"
#include "producer/event_producer.h"
#include "matching/matching_engine.h"
#include "matching/order_book.h"
#include "logging/event_log.h"
#include <memory>
#include <iostream>
#include <iomanip>
#include <typeinfo>
#include <vector>
#include <algorithm>
#include <utility>
#include <fstream>
#include <sstream>
#include <filesystem>

namespace exchange {

ExchangeSimulator::ExchangeSimulator() : running_(false) {
}

ExchangeSimulator::~ExchangeSimulator() {
    shutdown();
}

void ExchangeSimulator::configure(const SimulatorConfig& config) {
    config_ = config;
}

bool ExchangeSimulator::initialize() {
    // Initialize default components if not set
    if (!event_log_) {
        event_log_ = std::make_unique<DeterministicEventLog>();
    }
    
    if (!matching_engine_) {
        matching_engine_ = std::make_unique<PriceTimeMatchingEngine>();
        
        // Set up callbacks
        matching_engine_->set_trade_callback([this](const TradeEvent& trade) {
            on_trade(trade);
        });
        
        matching_engine_->set_book_update_callback([this](const BookUpdateEvent& update) {
            on_book_update(update);
        });
    }
    
    if (!event_producer_) {
        auto producer = std::make_unique<QRSDPEventProducer>();
        
        // Create default order book and symbol
        Symbol default_symbol{"DEFAULT"};
        auto order_book = std::make_shared<LimitOrderBook>(default_symbol);
        matching_engine_->set_order_book(default_symbol, order_book);
        
        producer->set_order_book(default_symbol, order_book);
        producer->set_tick_size(1);
        producer->set_horizon(3600ULL * 1000000000ULL); // 1 hour in nanoseconds
        
        event_producer_ = std::move(producer);
    }
    
    // Initialize event log with seed
    event_log_->initialize(config_.seed);
    
    // Initialize producer
    event_producer_->initialize(config_.seed);
    
    return true;
}

void ExchangeSimulator::run() {
    if (!running_) {
        running_ = true;
    }
    
    // Main simulation loop - run until producer stops or limit reached
    uint64_t max_iterations = 10000;  // Safety limit
    uint64_t iteration = 0;
    
    while (running_ && event_producer_ && event_producer_->has_next_event() && iteration < max_iterations) {
        OrderEvent event = event_producer_->next_event();
        
        // Skip invalid events (e.g., cancels when no orders exist)
        if (event.type == EventType::ORDER_CANCEL && event.order_id.value == 0) {
            continue;
        }
        
        process_order_event(event);
        iteration++;
    }
    
    running_ = false;
}

void ExchangeSimulator::shutdown() {
    running_ = false;
    if (streamer_) {
        streamer_->shutdown();
    }
}

void ExchangeSimulator::set_event_producer(std::unique_ptr<IEventProducer> producer) {
    event_producer_ = std::move(producer);
}

void ExchangeSimulator::set_matching_engine(std::unique_ptr<IMatchingEngine> engine) {
    matching_engine_ = std::move(engine);
}

void ExchangeSimulator::set_event_log(std::unique_ptr<IEventLog> log) {
    event_log_ = std::move(log);
}

void ExchangeSimulator::set_encoder(std::unique_ptr<IITCHEncoder> encoder) {
    encoder_ = std::move(encoder);
}

void ExchangeSimulator::set_streamer(std::unique_ptr<IUDPStreamer> streamer) {
    streamer_ = std::move(streamer);
}

const SimulatorConfig& ExchangeSimulator::get_config() const {
    return config_;
}

bool ExchangeSimulator::is_running() const {
    return running_;
}

void ExchangeSimulator::process_order_event(const OrderEvent& event) {
    // Set timestamp in matching engine from event timestamp (for determinism)
    if (matching_engine_) {
        auto price_time_engine = dynamic_cast<PriceTimeMatchingEngine*>(matching_engine_.get());
        if (price_time_engine) {
            price_time_engine->set_current_timestamp(event.timestamp);
        }
    }
    
    // Log event first (before processing) to maintain deterministic ordering
    if (event_log_) {
        event_log_->append_event(event);
    }
    // Then process through matching engine
    if (matching_engine_) {
        matching_engine_->process_order_event(event);
    }
}

void ExchangeSimulator::on_trade(const TradeEvent& trade) {
    if (event_log_) {
        event_log_->append_trade(trade);
    }
    if (encoder_ && streamer_ && config_.enable_udp_streaming) {
        auto encoded = encoder_->encode_trade(trade);
        streamer_->stream_message(encoded);
    }
}

void ExchangeSimulator::on_book_update(const BookUpdateEvent& update) {
    if (event_log_) {
        event_log_->append_book_update(update);
    }
    if (encoder_ && streamer_ && config_.enable_udp_streaming) {
        auto encoded = encoder_->encode_book_update(update);
        streamer_->stream_message(encoded);
    }
}

void ExchangeSimulator::print_event_log_summary() const {
    if (!event_log_) {
        std::cout << "No event log available" << std::endl;
        return;
    }
    
    auto deterministic_log = dynamic_cast<const DeterministicEventLog*>(event_log_.get());
    if (!deterministic_log) {
        std::cout << "Event log type not supported for visualization" << std::endl;
        return;
    }
    
    const auto& order_events = deterministic_log->get_order_events();
    const auto& trade_events = deterministic_log->get_trade_events();
    const auto& book_updates = deterministic_log->get_book_update_events();
    
    std::cout << "\n" << std::string(70, '=') << std::endl;
    std::cout << "                    EVENT LOG SUMMARY" << std::endl;
    std::cout << std::string(70, '=') << std::endl;
    
    std::cout << "\nConfiguration:" << std::endl;
    std::cout << "  Seed: " << event_log_->get_seed() << std::endl;
    std::cout << "  Total Sequence: " << event_log_->get_sequence_number() << std::endl;
    
    std::cout << "\nEvent Counts:" << std::endl;
    std::cout << "  Order Events: " << order_events.size() << std::endl;
    std::cout << "  Trade Events: " << trade_events.size() << std::endl;
    std::cout << "  Book Updates: " << book_updates.size() << std::endl;
    
    // Count by type
    size_t adds = 0, cancels = 0, aggressive = 0;
    for (const auto& event : order_events) {
        switch (event.type) {
            case EventType::ORDER_ADD: adds++; break;
            case EventType::ORDER_CANCEL: cancels++; break;
            case EventType::ORDER_AGGRESSIVE_TAKE: aggressive++; break;
            default: break;
        }
    }
    
    std::cout << "\nOrder Event Breakdown:" << std::endl;
    std::cout << "  ADDs: " << adds << std::endl;
    std::cout << "  CANCELs: " << cancels << std::endl;
    std::cout << "  AGGRESSIVE: " << aggressive << std::endl;
    
    if (!trade_events.empty()) {
        uint64_t total_volume = 0;
        double total_notional = 0.0;
        for (const auto& trade : trade_events) {
            total_volume += trade.execution_quantity.value;
            total_notional += static_cast<double>(trade.execution_price.value) * trade.execution_quantity.value;
        }
        std::cout << "\nTrade Statistics:" << std::endl;
        std::cout << "  Total Volume: " << total_volume << std::endl;
        std::cout << "  Total Notional: " << std::fixed << std::setprecision(2) << total_notional << std::endl;
        if (total_volume > 0) {
            std::cout << "  Average Price: " << std::fixed << std::setprecision(2) << (total_notional / total_volume) << std::endl;
        }
    }
    
    // Show first 20 order events
    std::cout << "\n" << std::string(70, '-') << std::endl;
    std::cout << "First 20 Order Events:" << std::endl;
    std::cout << std::string(70, '-') << std::endl;
    std::cout << std::left << std::setw(8) << "Seq" 
              << std::setw(12) << "Type" 
              << std::setw(8) << "Side"
              << std::setw(10) << "Price"
              << std::setw(10) << "Qty"
              << std::setw(15) << "Timestamp (ns)" << std::endl;
    std::cout << std::string(70, '-') << std::endl;
    
    size_t count = 0;
    for (const auto& event : order_events) {
        if (count >= 20) break;
        
        std::string type_str;
        switch (event.type) {
            case EventType::ORDER_ADD: type_str = "ADD"; break;
            case EventType::ORDER_CANCEL: type_str = "CANCEL"; break;
            case EventType::ORDER_AGGRESSIVE_TAKE: type_str = "AGGRESSIVE"; break;
            default: type_str = "UNKNOWN"; break;
        }
        
        std::string side_str = (event.side == OrderSide::BUY) ? "BUY" : "SELL";
        
        std::cout << std::left << std::setw(8) << event.sequence_number
                  << std::setw(12) << type_str
                  << std::setw(8) << side_str
                  << std::setw(10) << event.price.value
                  << std::setw(10) << event.quantity.value
                  << std::setw(15) << event.timestamp.nanoseconds_since_epoch << std::endl;
        count++;
    }
    
    // Show first 10 trades
    if (!trade_events.empty()) {
        std::cout << "\n" << std::string(70, '-') << std::endl;
        std::cout << "First 10 Trade Events:" << std::endl;
        std::cout << std::string(70, '-') << std::endl;
        std::cout << std::left << std::setw(8) << "Seq"
                  << std::setw(10) << "Buy ID"
                  << std::setw(10) << "Sell ID"
                  << std::setw(10) << "Price"
                  << std::setw(10) << "Qty"
                  << std::setw(15) << "Timestamp (ns)" << std::endl;
        std::cout << std::string(70, '-') << std::endl;
        
        count = 0;
        for (const auto& trade : trade_events) {
            if (count >= 10) break;
            
            std::cout << std::left << std::setw(8) << trade.sequence_number
                      << std::setw(10) << trade.buy_order_id.value
                      << std::setw(10) << trade.sell_order_id.value
                      << std::setw(10) << trade.execution_price.value
                      << std::setw(10) << trade.execution_quantity.value
                      << std::setw(15) << trade.timestamp.nanoseconds_since_epoch << std::endl;
            count++;
        }
    }
    
    // Price over time chart
    if (!trade_events.empty()) {
        std::cout << "\n" << std::string(70, '-') << std::endl;
        std::cout << "Price Over Time (Last 50 Trades):" << std::endl;
        std::cout << std::string(70, '-') << std::endl;
        
        // Collect prices from trades (use last 50 for chart)
        std::vector<std::pair<uint64_t, uint32_t>> price_points; // timestamp, price
        size_t start_idx = (trade_events.size() > 50) ? trade_events.size() - 50 : 0;
        for (size_t i = start_idx; i < trade_events.size(); ++i) {
            price_points.push_back({trade_events[i].timestamp.nanoseconds_since_epoch, 
                                   trade_events[i].execution_price.value});
        }
        
        if (!price_points.empty()) {
            // Find min/max for scaling
            uint32_t min_price = price_points[0].second;
            uint32_t max_price = price_points[0].second;
            
            for (const auto& point : price_points) {
                if (point.second < min_price) min_price = point.second;
                if (point.second > max_price) max_price = point.second;
            }
            
            // Calculate price range and chart dimensions
            uint32_t price_range = max_price - min_price;
            if (price_range == 0) price_range = 1; // Avoid division by zero
            const int chart_height = 20;
            const int chart_width = 60;
            
            // Create chart grid
            std::vector<std::vector<char>> chart(chart_height, std::vector<char>(chart_width, ' '));
            
            // Plot price points and connect with lines
            for (size_t i = 0; i < price_points.size() && i < static_cast<size_t>(chart_width); ++i) {
                int x = static_cast<int>(i);
                // Scale price to chart height (invert Y axis - higher prices at top)
                int y = chart_height - 1 - static_cast<int>((price_points[i].second - min_price) * (chart_height - 1) / price_range);
                if (y < 0) y = 0;
                if (y >= chart_height) y = chart_height - 1;
                
                // Plot point
                chart[y][x] = '*';
                
                // Connect to previous point with line
                if (i > 0) {
                    int prev_x = static_cast<int>(i - 1);
                    int prev_y = chart_height - 1 - static_cast<int>((price_points[i-1].second - min_price) * (chart_height - 1) / price_range);
                    if (prev_y < 0) prev_y = 0;
                    if (prev_y >= chart_height) prev_y = chart_height - 1;
                    
                    // Draw line between points
                    int dx = x - prev_x;
                    int dy = y - prev_y;
                    int steps = std::max(std::abs(dx), std::abs(dy));
                    if (steps > 0) {
                        for (int step = 1; step < steps; ++step) {
                            int line_x = prev_x + (dx * step) / steps;
                            int line_y = prev_y + (dy * step) / steps;
                            if (line_x >= 0 && line_x < chart_width && line_y >= 0 && line_y < chart_height) {
                                if (chart[line_y][line_x] == ' ') {
                                    chart[line_y][line_x] = '.';
                                }
                            }
                        }
                    }
                }
            }
            
            // Draw chart with axes
            std::cout << "Price\n";
            for (int y = 0; y < chart_height; ++y) {
                // Y-axis label (price)
                uint32_t price_at_y = max_price - static_cast<uint32_t>((y * price_range) / (chart_height - 1));
                std::cout << std::right << std::setw(6) << price_at_y << " |";
                
                // Chart line
                for (int x = 0; x < chart_width; ++x) {
                    std::cout << chart[y][x];
                }
                std::cout << "\n";
            }
            
            // X-axis
            std::cout << "       +";
            for (int x = 0; x < chart_width; ++x) {
                std::cout << "-";
            }
            std::cout << "\n";
            std::cout << "        Time (trade sequence)\n";
            
            // Show price statistics
            std::cout << "\nPrice Range: " << min_price << " - " << max_price 
                      << " (range: " << price_range << " ticks)\n";
        }
    }
    
    std::cout << "\n" << std::string(70, '=') << std::endl;
}

void ExchangeSimulator::export_price_data_to_csv(const std::string& filename) const {
    if (!event_log_) {
        std::cerr << "No event log available for CSV export" << std::endl;
        return;
    }
    
    auto deterministic_log = dynamic_cast<const DeterministicEventLog*>(event_log_.get());
    if (!deterministic_log) {
        std::cerr << "Event log type not supported for CSV export" << std::endl;
        return;
    }
    
    const auto& trade_events = deterministic_log->get_trade_events();
    
    // Ensure directory exists before writing
    std::filesystem::path file_path(filename);
    std::filesystem::create_directories(file_path.parent_path());
    
    std::ofstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Failed to open file for writing: " << filename << std::endl;
        return;
    }
    
    // Write CSV header
    file << "timestamp_ns,sequence_number,price,quantity,buy_order_id,sell_order_id\n";
    
    // Write trade events (price data)
    for (const auto& trade : trade_events) {
        file << trade.timestamp.nanoseconds_since_epoch << ","
             << trade.sequence_number << ","
             << trade.execution_price.value << ","
             << trade.execution_quantity.value << ","
             << trade.buy_order_id.value << ","
             << trade.sell_order_id.value << "\n";
    }
    
    file.close();
    std::cout << "\nPrice data exported to: " << filename << std::endl;
    std::cout << "  Total trades: " << trade_events.size() << std::endl;
    std::cout << "  You can open this CSV in Excel, Python, or any data visualization tool" << std::endl;
}

} // namespace exchange

