#pragma once

#include <vector>
#include <cstdint>
#include <string>
#include <memory>
#include <functional>

namespace exchange {

struct StreamConfig {
    std::string host;
    uint16_t port;
    uint32_t packet_loss_percentage; // 0-100
    uint64_t latency_microseconds; // Optional latency simulation
    bool enabled;
};

class IUDPStreamer {
public:
    virtual ~IUDPStreamer() = default;
    
    virtual void configure(const StreamConfig& config) = 0;
    virtual bool initialize() = 0;
    virtual void stream_message(const std::vector<uint8_t>& message) = 0;
    virtual void stream_batch(const std::vector<std::vector<uint8_t>>& messages) = 0;
    virtual void shutdown() = 0;
    
    virtual bool is_connected() const = 0;
    virtual StreamConfig get_config() const = 0;
};

class UDPStreamer : public IUDPStreamer {
public:
    UDPStreamer();
    virtual ~UDPStreamer();
    
    void configure(const StreamConfig& config) override;
    bool initialize() override;
    void stream_message(const std::vector<uint8_t>& message) override;
    void stream_batch(const std::vector<std::vector<uint8_t>>& messages) override;
    void shutdown() override;
    
    bool is_connected() const override;
    StreamConfig get_config() const override;
    
private:
    StreamConfig config_;
    void* socket_handle_; // Platform-specific socket handle (void* for now)
    bool connected_;
    
    bool should_drop_packet() const;
    void apply_latency_simulation() const;
};

} // namespace exchange

