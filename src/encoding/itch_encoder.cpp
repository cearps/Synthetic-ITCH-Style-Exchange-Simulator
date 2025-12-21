#include "encoding/itch_encoder.h"

namespace exchange {

ITCHEncoder::ITCHEncoder() {
}

std::vector<uint8_t> ITCHEncoder::encode_order_add(const OrderEvent& event) {
    (void)event;  // Unused in stub
    // TODO: Implement ORDER_ADD encoding
    return std::vector<uint8_t>();
}

std::vector<uint8_t> ITCHEncoder::encode_order_cancel(const OrderEvent& event) {
    (void)event;  // Unused in stub
    // TODO: Implement ORDER_CANCEL encoding
    return std::vector<uint8_t>();
}

std::vector<uint8_t> ITCHEncoder::encode_order_aggressive_take(const OrderEvent& event) {
    (void)event;  // Unused in stub
    // TODO: Implement aggressive take encoding
    return std::vector<uint8_t>();
}

std::vector<uint8_t> ITCHEncoder::encode_trade(const TradeEvent& trade) {
    (void)trade;  // Unused in stub
    // TODO: Implement trade encoding
    return std::vector<uint8_t>();
}

std::vector<uint8_t> ITCHEncoder::encode_book_update(const BookUpdateEvent& update) {
    (void)update;  // Unused in stub
    // TODO: Implement book update encoding
    return std::vector<uint8_t>();
}

bool ITCHEncoder::decode_message(const std::vector<uint8_t>& data, OrderEvent& event) {
    (void)data;   // Unused in stub
    (void)event;  // Unused in stub
    // TODO: Implement decoding
    return false;
}

bool ITCHEncoder::decode_message(const std::vector<uint8_t>& data, TradeEvent& trade) {
    (void)data;   // Unused in stub
    (void)trade;  // Unused in stub
    // TODO: Implement decoding
    return false;
}

bool ITCHEncoder::decode_message(const std::vector<uint8_t>& data, BookUpdateEvent& update) {
    (void)data;   // Unused in stub
    (void)update; // Unused in stub
    // TODO: Implement decoding
    return false;
}

std::vector<uint8_t> ITCHEncoder::encode_header(uint8_t message_type, uint16_t message_length) const {
    (void)message_type;   // Unused in stub
    (void)message_length; // Unused in stub
    // TODO: Implement header encoding
    return std::vector<uint8_t>();
}

} // namespace exchange

