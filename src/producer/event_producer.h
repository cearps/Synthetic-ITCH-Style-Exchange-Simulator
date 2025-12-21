#pragma once

#include "../core/events.h"
#include "../matching/order_book.h"
#include <memory>
#include <functional>
#include <random>

namespace exchange {

class IEventProducer {
public:
    virtual ~IEventProducer() = default;
    
    virtual void initialize(uint64_t seed) = 0;
    virtual bool has_next_event() const = 0;
    virtual OrderEvent next_event() = 0;
    virtual void reset() = 0;
};

class QRSDPEventProducer : public IEventProducer {
public:
    QRSDPEventProducer();
    virtual ~QRSDPEventProducer() = default;
    
    void initialize(uint64_t seed) override;
    bool has_next_event() const override;
    OrderEvent next_event() override;
    void reset() override;
    
    void set_order_book(Symbol symbol, std::shared_ptr<IOrderBook> order_book);
    void set_tick_size(int64_t tick_size);
    void set_horizon(uint64_t max_time_ns);
    
private:
    uint64_t seed_;
    uint64_t order_id_counter_;
    uint64_t sequence_counter_;
    uint64_t current_time_ns_;
    uint64_t max_time_ns_;
    int64_t tick_size_;
    bool has_pending_event_;
    OrderEvent pending_event_;
    
    std::shared_ptr<IOrderBook> order_book_;
    Symbol symbol_;
    
    std::mt19937 rng_;
    std::uniform_real_distribution<double> uniform_dist_;
    std::exponential_distribution<double> exp_dist_;
    
    // State bucketing
    enum class SpreadBucket { S1, S2, S3 };
    enum class ImbalanceBucket { I_NEG_NEG, I_NEG, I_ZERO, I_POS, I_POS_POS };
    enum class QueueBucket { Q_SMALL, Q_MED, Q_LARGE };
    
    struct BookState {
        SpreadBucket spread_bucket;
        ImbalanceBucket imbalance_bucket;
        QueueBucket bid_queue_bucket;
        QueueBucket ask_queue_bucket;
        Price best_bid;
        Price best_ask;
        Quantity bid_qty;
        Quantity ask_qty;
    };
    
    BookState read_book_state() const;
    SpreadBucket bucket_spread(int64_t spread_ticks) const;
    ImbalanceBucket bucket_imbalance(double imbalance) const;
    QueueBucket bucket_queue_size(Quantity qty) const;
    
    // Intensity lookup tables (simplified v0)
    double get_intensity_add_bid(const BookState& state) const;
    double get_intensity_add_ask(const BookState& state) const;
    double get_intensity_cancel_bid(const BookState& state) const;
    double get_intensity_cancel_ask(const BookState& state) const;
    double get_intensity_take_buy(const BookState& state) const;
    double get_intensity_take_sell(const BookState& state) const;
    
    // Event generation
    OrderEvent generate_add_limit_bid(const BookState& state);
    OrderEvent generate_add_limit_ask(const BookState& state);
    OrderEvent generate_cancel_bid(const BookState& state);
    OrderEvent generate_cancel_ask(const BookState& state);
    OrderEvent generate_aggressive_buy(const BookState& state);
    OrderEvent generate_aggressive_sell(const BookState& state);
    
    Price sample_price_for_add(OrderSide side, const BookState& state);
    Quantity sample_quantity();
    OrderId next_order_id();
};

} // namespace exchange

