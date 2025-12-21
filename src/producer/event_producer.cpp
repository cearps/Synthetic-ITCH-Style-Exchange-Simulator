#include "producer/event_producer.h"
#include "matching/order_book.h"
#include <algorithm>
#include <cmath>

namespace exchange {

QRSDPEventProducer::QRSDPEventProducer()
    : seed_(0), order_id_counter_(1), sequence_counter_(0), current_time_ns_(0), 
      max_time_ns_(UINT64_MAX), tick_size_(1), has_pending_event_(false),
      uniform_dist_(0.0, 1.0), exp_dist_(1.0), normal_dist_(0.0, 1.0),
      reference_price_(10050.0), price_drift_(0.0), price_volatility_(0.2),
      last_price_update_ns_(0), current_volatility_(0.2) {
}

void QRSDPEventProducer::initialize(uint64_t seed) {
    seed_ = seed;
    order_id_counter_ = 1;
    sequence_counter_ = 0;
    current_time_ns_ = 0;
    has_pending_event_ = false;
    rng_.seed(static_cast<unsigned int>(seed));
    
    // Initialize reference price dynamics
    // Start at a reasonable mid-price (10050 = midpoint of typical 10000-10100 range)
    reference_price_ = 10050.0;
    
    // Annualized parameters (convert to per-nanosecond)
    // Drift: Small random drift component (can be positive or negative)
    // This creates trends in the price movement
    double annual_drift = (uniform_dist_(rng_) - 0.5) * 0.1;  // -5% to +5% annual drift
    price_drift_ = annual_drift / (365.25 * 24 * 3600 * 1e9);
    
    // Volatility: 30% annualized (higher for more realistic movement)
    // Convert to per-nanosecond: sigma / sqrt(nanoseconds_per_year)
    double nanoseconds_per_year = 365.25 * 24 * 3600 * 1e9;
    price_volatility_ = 0.3 / std::sqrt(nanoseconds_per_year);
    current_volatility_ = price_volatility_;
    
    last_price_update_ns_ = 0;
}

bool QRSDPEventProducer::has_next_event() const {
    if (!order_book_) {
        return false;
    }
    
    // Check if we've exceeded time horizon
    if (current_time_ns_ >= max_time_ns_) {
        return false;
    }
    
    // Always return true if we have a pending event or can generate one
    return true;
}

OrderEvent QRSDPEventProducer::next_event() {
    if (has_pending_event_) {
        has_pending_event_ = false;
        return pending_event_;
    }
    
    if (!order_book_) {
        OrderEvent empty{};
        return empty;
    }
    
    // Update reference price (geometric Brownian motion with mean reversion)
    update_reference_price();
    
    // Read current book state
    BookState state = read_book_state();
    
    // Compute intensities for all event families
    double lambda_add_bid = get_intensity_add_bid(state);
    double lambda_add_ask = get_intensity_add_ask(state);
    double lambda_cancel_bid = get_intensity_cancel_bid(state);
    double lambda_cancel_ask = get_intensity_cancel_ask(state);
    double lambda_take_buy = get_intensity_take_buy(state);
    double lambda_take_sell = get_intensity_take_sell(state);
    
    double lambda_total = lambda_add_bid + lambda_add_ask + 
                         lambda_cancel_bid + lambda_cancel_ask +
                         lambda_take_buy + lambda_take_sell;
    
    // Fallback if no events possible (empty book scenario)
    if (lambda_total <= 0.0) {
        // Default to small add rates to keep simulation alive
        // This handles the case when book is empty and we can't cancel
        lambda_add_bid = 1.0;
        lambda_add_ask = 1.0;
        lambda_total = 2.0;
    }
    
    // Ensure we don't exceed time horizon
    if (current_time_ns_ >= max_time_ns_) {
        OrderEvent empty{};
        return empty;
    }
    
    // Sample time to next event (exponential distribution)
    exp_dist_.param(std::exponential_distribution<double>::param_type(lambda_total));
    double delta_t = exp_dist_(rng_);
    current_time_ns_ += static_cast<uint64_t>(delta_t * 1e9); // Convert to nanoseconds
    
    // Sample which event occurs (categorical distribution)
    double rand = uniform_dist_(rng_) * lambda_total;
    OrderEvent event{};
    
    if (rand < lambda_add_bid) {
        event = generate_add_limit_bid(state);
    } else if (rand < lambda_add_bid + lambda_add_ask) {
        event = generate_add_limit_ask(state);
    } else if (rand < lambda_add_bid + lambda_add_ask + lambda_cancel_bid) {
        event = generate_cancel_bid(state);
    } else if (rand < lambda_add_bid + lambda_add_ask + lambda_cancel_bid + lambda_cancel_ask) {
        event = generate_cancel_ask(state);
    } else if (rand < lambda_add_bid + lambda_add_ask + lambda_cancel_bid + lambda_cancel_ask + lambda_take_buy) {
        event = generate_aggressive_buy(state);
    } else {
        event = generate_aggressive_sell(state);
    }
    
    event.timestamp.nanoseconds_since_epoch = current_time_ns_;
    // Sequence number will be set by event log when appended
    
    return event;
}

void QRSDPEventProducer::reset() {
    order_id_counter_ = 1;
    current_time_ns_ = 0;
    has_pending_event_ = false;
    sequence_counter_ = 0;
    rng_.seed(static_cast<unsigned int>(seed_));
}

void QRSDPEventProducer::set_order_book(Symbol symbol, std::shared_ptr<IOrderBook> order_book) {
    symbol_ = symbol;
    order_book_ = order_book;
}

void QRSDPEventProducer::set_tick_size(int64_t tick_size) {
    tick_size_ = tick_size;
}

void QRSDPEventProducer::set_horizon(uint64_t max_time_ns) {
    max_time_ns_ = max_time_ns;
}

QRSDPEventProducer::BookState QRSDPEventProducer::read_book_state() const {
    BookState state{};
    
    if (!order_book_) {
        return state;
    }
    
    state.best_bid = order_book_->has_bid() ? order_book_->best_bid() : Price{0};
    state.best_ask = order_book_->has_ask() ? order_book_->best_ask() : Price{0};
    state.bid_qty = order_book_->has_bid() ? order_book_->bid_quantity_at_price(state.best_bid) : Quantity{0};
    state.ask_qty = order_book_->has_ask() ? order_book_->ask_quantity_at_price(state.best_ask) : Quantity{0};
    
    // Calculate spread
    int64_t spread_ticks = 0;
    if (order_book_->has_bid() && order_book_->has_ask()) {
        spread_ticks = (state.best_ask.value - state.best_bid.value) / tick_size_;
    }
    state.spread_bucket = bucket_spread(spread_ticks);
    
    // Calculate imbalance
    double imbalance = 0.0;
    uint64_t total_qty = state.bid_qty.value + state.ask_qty.value;
    if (total_qty > 0) {
        imbalance = static_cast<double>(static_cast<int64_t>(state.bid_qty.value) - static_cast<int64_t>(state.ask_qty.value)) / total_qty;
    }
    state.imbalance_bucket = bucket_imbalance(imbalance);
    
    // Bucket queue sizes
    state.bid_queue_bucket = bucket_queue_size(state.bid_qty);
    state.ask_queue_bucket = bucket_queue_size(state.ask_qty);
    
    return state;
}

QRSDPEventProducer::SpreadBucket QRSDPEventProducer::bucket_spread(int64_t spread_ticks) const {
    if (spread_ticks <= 1) return SpreadBucket::S1;
    if (spread_ticks == 2) return SpreadBucket::S2;
    return SpreadBucket::S3;
}

QRSDPEventProducer::ImbalanceBucket QRSDPEventProducer::bucket_imbalance(double imbalance) const {
    if (imbalance < -0.6) return ImbalanceBucket::I_NEG_NEG;
    if (imbalance < -0.2) return ImbalanceBucket::I_NEG;
    if (imbalance <= 0.2) return ImbalanceBucket::I_ZERO;
    if (imbalance <= 0.6) return ImbalanceBucket::I_POS;
    return ImbalanceBucket::I_POS_POS;
}

QRSDPEventProducer::QueueBucket QRSDPEventProducer::bucket_queue_size(Quantity qty) const {
    if (qty.value < 100) return QueueBucket::Q_SMALL;
    if (qty.value < 1000) return QueueBucket::Q_MED;
    return QueueBucket::Q_LARGE;
}

// Simplified intensity lookup tables (v0 - can be calibrated later)
double QRSDPEventProducer::get_intensity_add_bid(const BookState& state) const {
    // Base rate, adjusted by state
    double base = 10.0;
    if (state.spread_bucket == SpreadBucket::S1) base *= 0.8;  // Tighter spread, less passive adds
    if (state.imbalance_bucket == ImbalanceBucket::I_POS_POS) base *= 1.2;  // Bid-heavy, more ask adds
    return base;
}

double QRSDPEventProducer::get_intensity_add_ask(const BookState& state) const {
    double base = 10.0;
    if (state.spread_bucket == SpreadBucket::S1) base *= 0.8;
    if (state.imbalance_bucket == ImbalanceBucket::I_NEG_NEG) base *= 1.2;  // Ask-heavy, more bid adds
    return base;
}

double QRSDPEventProducer::get_intensity_cancel_bid(const BookState& state) const {
    double base = 5.0;
    if (state.bid_queue_bucket == QueueBucket::Q_LARGE) base *= 1.5;  // Large queues, more cancels
    return base;
}

double QRSDPEventProducer::get_intensity_cancel_ask(const BookState& state) const {
    double base = 5.0;
    if (state.ask_queue_bucket == QueueBucket::Q_LARGE) base *= 1.5;
    return base;
}

double QRSDPEventProducer::get_intensity_take_buy(const BookState& state) const {
    double base = 3.0;
    if (state.spread_bucket == SpreadBucket::S1) base *= 2.0;  // Tighter spread, more taking
    if (state.imbalance_bucket == ImbalanceBucket::I_POS_POS) base *= 1.3;  // Bid-heavy, more aggressive sells
    return base;
}

double QRSDPEventProducer::get_intensity_take_sell(const BookState& state) const {
    double base = 3.0;
    if (state.spread_bucket == SpreadBucket::S1) base *= 2.0;
    if (state.imbalance_bucket == ImbalanceBucket::I_NEG_NEG) base *= 1.3;  // Ask-heavy, more aggressive buys
    return base;
}

OrderEvent QRSDPEventProducer::generate_add_limit_bid(const BookState& state) {
    OrderEvent event{};
    event.type = EventType::ORDER_ADD;
    event.order_id = next_order_id();
    event.symbol = symbol_;
    event.side = OrderSide::BUY;
    event.order_type = OrderType::LIMIT;
    event.price = sample_price_for_add(OrderSide::BUY, state);
    event.quantity = sample_quantity();
    return event;
}

OrderEvent QRSDPEventProducer::generate_add_limit_ask(const BookState& state) {
    OrderEvent event{};
    event.type = EventType::ORDER_ADD;
    event.order_id = next_order_id();
    event.symbol = symbol_;
    event.side = OrderSide::SELL;
    event.order_type = OrderType::LIMIT;
    event.price = sample_price_for_add(OrderSide::SELL, state);
    event.quantity = sample_quantity();
    return event;
}

OrderEvent QRSDPEventProducer::generate_cancel_bid(const BookState& state) {
    OrderEvent event{};
    event.type = EventType::ORDER_CANCEL;
    event.symbol = symbol_;
    event.side = OrderSide::BUY;
    event.order_type = OrderType::LIMIT;
    
    // Find an order to cancel (simplified - get first order at best bid)
    if (order_book_->has_bid()) {
        auto limit_book = std::dynamic_pointer_cast<LimitOrderBook>(order_book_);
        if (limit_book) {
            auto order = limit_book->get_first_bid_order_at_price(state.best_bid);
            if (order) {
                event.order_id = order->id();
                event.price = order->price();
                // Partial cancel (50-100% of remaining)
                double cancel_ratio = 0.5 + uniform_dist_(rng_) * 0.5;
                event.quantity.value = static_cast<uint64_t>(order->remaining_quantity().value * cancel_ratio);
            }
        }
    }
    
    return event;
}

OrderEvent QRSDPEventProducer::generate_cancel_ask(const BookState& state) {
    OrderEvent event{};
    event.type = EventType::ORDER_CANCEL;
    event.symbol = symbol_;
    event.side = OrderSide::SELL;
    event.order_type = OrderType::LIMIT;
    
    if (order_book_ && order_book_->has_ask()) {
        auto limit_book = std::dynamic_pointer_cast<LimitOrderBook>(order_book_);
        if (limit_book) {
            auto order = limit_book->get_first_ask_order_at_price(state.best_ask);
            if (order && order->is_active()) {
                event.order_id = order->id();
                event.price = order->price();
                double cancel_ratio = 0.5 + uniform_dist_(rng_) * 0.5;
                uint64_t cancel_qty = static_cast<uint64_t>(order->remaining_quantity().value * cancel_ratio);
                event.quantity.value = (cancel_qty > 0) ? cancel_qty : 1;
            }
        }
    }
    
    return event;
}

OrderEvent QRSDPEventProducer::generate_aggressive_buy(const BookState& /* state */) {
    OrderEvent event{};
    event.type = EventType::ORDER_AGGRESSIVE_TAKE;
    event.order_id = next_order_id();
    event.symbol = symbol_;
    event.side = OrderSide::BUY;
    event.order_type = OrderType::MARKET;
    event.price = Price{0};  // Market order
    event.quantity = sample_quantity();
    return event;
}

OrderEvent QRSDPEventProducer::generate_aggressive_sell(const BookState& /* state */) {
    OrderEvent event{};
    event.type = EventType::ORDER_AGGRESSIVE_TAKE;
    event.order_id = next_order_id();
    event.symbol = symbol_;
    event.side = OrderSide::SELL;
    event.order_type = OrderType::MARKET;
    event.price = Price{0};  // Market order
    event.quantity = sample_quantity();
    return event;
}

void QRSDPEventProducer::update_reference_price() {
    if (current_time_ns_ == last_price_update_ns_) {
        return;  // No time has passed
    }
    
    uint64_t delta_ns = current_time_ns_ - last_price_update_ns_;
    last_price_update_ns_ = current_time_ns_;
    
    // Geometric Brownian Motion with mean reversion
    // dS = S * (mu * dt + sigma * dW) - theta * (S - S0) * dt
    // where:
    //   mu = drift
    //   sigma = volatility
    //   theta = mean reversion speed
    //   S0 = long-term mean (initial reference price)
    
    double dt = static_cast<double>(delta_ns) / (365.25 * 24 * 3600 * 1e9);  // Convert to years
    double theta = 0.05;  // Weak mean reversion (half-life ~ 14 years) - allows trends to persist
    double S0 = 10050.0;  // Long-term mean price
    
    // Random shock (Wiener process)
    double dW = normal_dist_(rng_) * std::sqrt(dt);
    
    // Update volatility with clustering (GARCH-like)
    // Volatility tends to persist: high vol -> high vol, low vol -> low vol
    double vol_shock = std::abs(normal_dist_(rng_)) * 0.15;
    current_volatility_ = 0.90 * current_volatility_ + 0.10 * price_volatility_ + vol_shock * price_volatility_;
    current_volatility_ = std::max(0.1, std::min(0.6, current_volatility_));  // Clamp to reasonable range
    
    // Scale volatility for simulation time (make movements more visible in short simulations)
    // For short simulations, we want more noticeable price movements
    double scaled_vol = current_volatility_ * 100.0;  // Amplify for visibility
    
    // Geometric Brownian Motion component
    double drift_term = price_drift_ * dt;
    double diffusion_term = scaled_vol * dW;
    
    // Weak mean reversion component (allows trends to develop)
    double mean_reversion_term = -theta * (reference_price_ - S0) * dt;
    
    // Update reference price (log-normal to ensure positive prices)
    double log_price = std::log(reference_price_);
    log_price += drift_term + diffusion_term + mean_reversion_term / reference_price_;
    reference_price_ = std::exp(log_price);
    
    // Clamp to reasonable range (prevent extreme values)
    reference_price_ = std::max(5000.0, std::min(20000.0, reference_price_));
}

Price QRSDPEventProducer::sample_price_for_add(OrderSide side, const BookState& state) {
    double rand = uniform_dist_(rng_);
    Price price{0};
    
    // Use reference price as the center of the distribution
    int64_t ref_price_ticks = static_cast<int64_t>(reference_price_);
    
    if (side == OrderSide::BUY) {
        if (!order_book_->has_bid()) {
            // No bid, place order below reference price
            int64_t offset_ticks = static_cast<int64_t>(normal_dist_(rng_) * 5.0);  // ~5 tick spread
            price = Price{ref_price_ticks - std::abs(offset_ticks) * tick_size_};
        } else {
            // Use reference price with realistic distribution
            if (rand < 0.3) {
                // 30% at best bid (liquidity provision)
                price = state.best_bid;
            } else if (rand < 0.6) {
                // 30% near reference price (within 3 ticks) - more spread around reference
                int64_t offset = static_cast<int64_t>(normal_dist_(rng_) * 3.0);
                int64_t target = ref_price_ticks + offset * tick_size_;
                // Ensure it's below best ask if exists
                if (order_book_->has_ask() && target >= state.best_ask.value) {
                    target = state.best_ask.value - tick_size_;
                }
                price = Price{target};
            } else if (rand < 0.85) {
                // 25% slightly below reference (1-5 ticks) - wider range
                int64_t offset = 1 + static_cast<int64_t>(uniform_dist_(rng_) * 4);
                price = Price{ref_price_ticks - offset * tick_size_};
            } else {
                // 15% deeper (aggressive limit orders) - more aggressive orders
                int64_t offset = static_cast<int64_t>(std::abs(normal_dist_(rng_)) * 8.0 + 5);
                price = Price{ref_price_ticks - offset * tick_size_};
            }
            
            // Ensure price doesn't exceed best ask
            if (order_book_->has_ask() && price.value >= state.best_ask.value) {
                price = Price{state.best_ask.value - tick_size_};
            }
        }
    } else {  // SELL
        if (!order_book_->has_ask()) {
            // No ask, place order above reference price
            int64_t offset_ticks = static_cast<int64_t>(normal_dist_(rng_) * 5.0);
            price = Price{ref_price_ticks + std::abs(offset_ticks) * tick_size_};
        } else {
            if (rand < 0.3) {
                // 30% at best ask
                price = state.best_ask;
            } else if (rand < 0.6) {
                // 30% near reference price (within 3 ticks) - more spread around reference
                int64_t offset = static_cast<int64_t>(normal_dist_(rng_) * 3.0);
                int64_t target = ref_price_ticks + offset * tick_size_;
                // Ensure it's above best bid if exists
                if (order_book_->has_bid() && target <= state.best_bid.value) {
                    target = state.best_bid.value + tick_size_;
                }
                price = Price{target};
            } else if (rand < 0.85) {
                // 25% slightly above reference (1-5 ticks) - wider range
                int64_t offset = 1 + static_cast<int64_t>(uniform_dist_(rng_) * 4);
                price = Price{ref_price_ticks + offset * tick_size_};
            } else {
                // 15% deeper (aggressive limit orders) - more aggressive orders
                int64_t offset = static_cast<int64_t>(std::abs(normal_dist_(rng_)) * 8.0 + 5);
                price = Price{ref_price_ticks + offset * tick_size_};
            }
            
            // Ensure price doesn't go below best bid
            if (order_book_->has_bid() && price.value <= state.best_bid.value) {
                price = Price{state.best_bid.value + tick_size_};
            }
        }
    }
    
    // Ensure price is positive and reasonable
    if (price.value < 1000) price = Price{1000};
    if (price.value > 50000) price = Price{50000};
    
    return price;
}

Quantity QRSDPEventProducer::sample_quantity() {
    // Simple size distribution (can be refined)
    double rand = uniform_dist_(rng_);
    uint64_t qty = 0;
    
    if (rand < 0.5) {
        qty = 100;  // 50% small
    } else if (rand < 0.8) {
        qty = 200;  // 30% medium
    } else if (rand < 0.95) {
        qty = 500;  // 15% large
    } else {
        qty = 1000;  // 5% very large
    }
    
    return Quantity{qty};
}

OrderId QRSDPEventProducer::next_order_id() {
    return OrderId{order_id_counter_++};
}

} // namespace exchange
