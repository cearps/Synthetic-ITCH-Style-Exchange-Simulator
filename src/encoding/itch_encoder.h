#pragma once

#include "../core/events.h"
#include <vector>
#include <cstdint>

namespace exchange {

class IITCHEncoder {
public:
    virtual ~IITCHEncoder() = default;
    
    virtual std::vector<uint8_t> encode_order_add(const OrderEvent& event) = 0;
    virtual std::vector<uint8_t> encode_order_cancel(const OrderEvent& event) = 0;
    virtual std::vector<uint8_t> encode_order_aggressive_take(const OrderEvent& event) = 0;
    virtual std::vector<uint8_t> encode_trade(const TradeEvent& trade) = 0;
    virtual std::vector<uint8_t> encode_book_update(const BookUpdateEvent& update) = 0;
    
    virtual bool decode_message(const std::vector<uint8_t>& data, OrderEvent& event) = 0;
    virtual bool decode_message(const std::vector<uint8_t>& data, TradeEvent& trade) = 0;
    virtual bool decode_message(const std::vector<uint8_t>& data, BookUpdateEvent& update) = 0;
};

class ITCHEncoder : public IITCHEncoder {
public:
    ITCHEncoder();
    virtual ~ITCHEncoder() = default;
    
    std::vector<uint8_t> encode_order_add(const OrderEvent& event) override;
    std::vector<uint8_t> encode_order_cancel(const OrderEvent& event) override;
    std::vector<uint8_t> encode_order_aggressive_take(const OrderEvent& event) override;
    std::vector<uint8_t> encode_trade(const TradeEvent& trade) override;
    std::vector<uint8_t> encode_book_update(const BookUpdateEvent& update) override;
    
    bool decode_message(const std::vector<uint8_t>& data, OrderEvent& event) override;
    bool decode_message(const std::vector<uint8_t>& data, TradeEvent& trade) override;
    bool decode_message(const std::vector<uint8_t>& data, BookUpdateEvent& update) override;
    
private:
    std::vector<uint8_t> encode_header(uint8_t message_type, uint16_t message_length) const;
};

} // namespace exchange

