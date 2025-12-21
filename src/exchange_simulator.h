#pragma once

#include "producer/event_producer.h"
#include "matching/matching_engine.h"
#include "logging/event_log.h"
#include "encoding/itch_encoder.h"
#include "streaming/udp_streamer.h"
#include "core/events.h"
#include <memory>
#include <cstdint>
#include <map>

namespace exchange {

struct SimulatorConfig {
    uint64_t seed;
    StreamConfig stream_config;
    bool enable_udp_streaming;
    bool enable_replay_mode;
};

class ExchangeSimulator {
public:
    ExchangeSimulator();
    ~ExchangeSimulator();
    
    void configure(const SimulatorConfig& config);
    bool initialize();
    void run();
    void shutdown();
    
    void set_event_producer(std::unique_ptr<IEventProducer> producer);
    void set_matching_engine(std::unique_ptr<IMatchingEngine> engine);
    void set_event_log(std::unique_ptr<IEventLog> log);
    void set_encoder(std::unique_ptr<IITCHEncoder> encoder);
    void set_streamer(std::unique_ptr<IUDPStreamer> streamer);
    
    const SimulatorConfig& get_config() const;
    bool is_running() const;
    
private:
    SimulatorConfig config_;
    bool running_;
    
    std::unique_ptr<IEventProducer> event_producer_;
    std::unique_ptr<IMatchingEngine> matching_engine_;
    std::unique_ptr<IEventLog> event_log_;
    std::unique_ptr<IITCHEncoder> encoder_;
    std::unique_ptr<IUDPStreamer> streamer_;
    
    void process_order_event(const OrderEvent& event);
    void on_trade(const TradeEvent& trade);
    void on_book_update(const BookUpdateEvent& update);
};

} // namespace exchange

