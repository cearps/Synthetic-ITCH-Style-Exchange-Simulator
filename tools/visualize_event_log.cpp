#include "../src/logging/event_log.h"
#include "../src/core/events.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <map>

void print_timeline(const std::vector<exchange::OrderEvent>& events, size_t max_events = 50) {
    std::cout << "\n=== Event Timeline (first " << max_events << " events) ===" << std::endl;
    std::cout << std::left << std::setw(12) << "Seq" 
              << std::setw(15) << "Type" 
              << std::setw(10) << "Side"
              << std::setw(12) << "Price"
              << std::setw(10) << "Qty"
              << std::setw(15) << "OrderID" << std::endl;
    std::cout << std::string(80, '-') << std::endl;
    
    size_t count = 0;
    for (const auto& event : events) {
        if (count >= max_events) break;
        
        std::string type_str;
        switch (event.type) {
            case exchange::EventType::ORDER_ADD: type_str = "ADD"; break;
            case exchange::EventType::ORDER_CANCEL: type_str = "CANCEL"; break;
            case exchange::EventType::ORDER_AGGRESSIVE_TAKE: type_str = "AGGRESSIVE"; break;
            default: type_str = "UNKNOWN"; break;
        }
        
        std::string side_str = (event.side == exchange::OrderSide::BUY) ? "BUY" : "SELL";
        
        std::cout << std::left << std::setw(12) << event.sequence_number
                  << std::setw(15) << type_str
                  << std::setw(10) << side_str
                  << std::setw(12) << event.price.value
                  << std::setw(10) << event.quantity.value
                  << std::setw(15) << event.order_id.value << std::endl;
        count++;
    }
}

void print_statistics(const std::vector<exchange::OrderEvent>& events,
                     const std::vector<exchange::TradeEvent>& trades) {
    std::cout << "\n=== Statistics ===" << std::endl;
    
    std::map<exchange::EventType, size_t> event_counts;
    for (const auto& event : events) {
        event_counts[event.type]++;
    }
    
    std::cout << "Order Events by Type:" << std::endl;
    for (const auto& pair : event_counts) {
        std::string type_str;
        switch (pair.first) {
            case exchange::EventType::ORDER_ADD: type_str = "ADD"; break;
            case exchange::EventType::ORDER_CANCEL: type_str = "CANCEL"; break;
            case exchange::EventType::ORDER_AGGRESSIVE_TAKE: type_str = "AGGRESSIVE"; break;
            default: type_str = "UNKNOWN"; break;
        }
        std::cout << "  " << type_str << ": " << pair.second << std::endl;
    }
    
    std::cout << "Total Trades: " << trades.size() << std::endl;
    
    if (!trades.empty()) {
        uint64_t total_volume = 0;
        int64_t total_value = 0;
        for (const auto& trade : trades) {
            total_volume += trade.execution_quantity.value;
            total_value += trade.execution_price.value * static_cast<int64_t>(trade.execution_quantity.value);
        }
        std::cout << "Total Volume: " << total_volume << std::endl;
        if (total_volume > 0) {
            std::cout << "Average Price: " << (total_value / static_cast<int64_t>(total_volume)) << std::endl;
        }
    }
}

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    
    std::cout << "Event Log Visualizer" << std::endl;
    std::cout << "===================" << std::endl;
    std::cout << "\nThis tool visualizes event logs from the simulator." << std::endl;
    std::cout << "Usage: Run simulator first to generate events, then use this tool." << std::endl;
    std::cout << "\nNote: Full implementation requires event log to be accessible." << std::endl;
    std::cout << "      For now, events are stored in memory in DeterministicEventLog." << std::endl;
    
    return 0;
}
