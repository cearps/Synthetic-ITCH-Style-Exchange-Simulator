#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace qrsdp {
namespace itch {

/// Fire-and-forget UDP sender supporting both multicast and unicast.
/// Cross-platform: uses Winsock on Windows, POSIX sockets elsewhere.
class UdpMulticastSender {
public:
    /// Multicast mode: sends to a multicast group.
    /// @param group  Multicast group address (e.g. "239.1.1.1").
    /// @param port   Destination port.
    /// @param ttl    Multicast TTL (default 1 = local subnet only).
    UdpMulticastSender(const std::string& group, uint16_t port, uint8_t ttl = 1);
    ~UdpMulticastSender();

    UdpMulticastSender(const UdpMulticastSender&) = delete;
    UdpMulticastSender& operator=(const UdpMulticastSender&) = delete;

    /// Unicast mode factory: sends to a specific host:port via plain UDP.
    /// Resolves the hostname at construction time.
    static std::unique_ptr<UdpMulticastSender> createUnicast(const std::string& host, uint16_t port);

    /// Send a datagram. Returns true on success.
    bool send(const uint8_t* data, size_t len);

private:
    UdpMulticastSender();  // used by createUnicast

#ifdef _WIN32
    uintptr_t sock_;  // SOCKET is uintptr_t on Windows
#else
    int sock_;
#endif
    struct SockAddr;
    SockAddr* dest_;
};

}  // namespace itch
}  // namespace qrsdp
