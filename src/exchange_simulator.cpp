#include "exchange_simulator.h"

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
    // TODO: Initialize default components if not set
    // TODO: Initialize event log with seed
    // TODO: Set up matching engine callbacks
    // TODO: Initialize UDP streamer if enabled
    return true;
}

void ExchangeSimulator::run() {
    running_ = true;
    // TODO: Main simulation loop
    while (running_ && event_producer_ && event_producer_->has_next_event()) {
        OrderEvent event = event_producer_->next_event();
        process_order_event(event);
    }
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
    if (matching_engine_) {
        matching_engine_->process_order_event(event);
    }
    if (event_log_) {
        event_log_->append_event(event);
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

} // namespace exchange

