#include "itch/udp_sender.h"

#include <cstdio>
#include <cstring>
#include <stdexcept>

#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
    #endif
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")

    namespace {
    struct WinsockInit {
        WinsockInit() {
            WSADATA wsa;
            WSAStartup(MAKEWORD(2, 2), &wsa);
        }
        ~WinsockInit() { WSACleanup(); }
    };
    static WinsockInit g_winsock_init;
    }  // namespace

    using socket_t = SOCKET;
    constexpr socket_t kInvalidSocket = INVALID_SOCKET;
    inline int closeSocket(socket_t s) { return closesocket(s); }
#else
    #include <arpa/inet.h>
    #include <netdb.h>
    #include <netinet/in.h>
    #include <sys/socket.h>
    #include <unistd.h>

    using socket_t = int;
    constexpr socket_t kInvalidSocket = -1;
    inline int closeSocket(socket_t s) { return close(s); }
#endif

namespace qrsdp {
namespace itch {

struct UdpMulticastSender::SockAddr {
    struct sockaddr_in addr;
};

UdpMulticastSender::UdpMulticastSender()
    : sock_(static_cast<decltype(sock_)>(kInvalidSocket)), dest_(nullptr) {}

std::unique_ptr<UdpMulticastSender> UdpMulticastSender::createUnicast(
    const std::string& host, uint16_t port)
{
    auto sender = std::unique_ptr<UdpMulticastSender>(new UdpMulticastSender());

    sender->sock_ = static_cast<decltype(sender->sock_)>(
        socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP));
    if (static_cast<socket_t>(sender->sock_) == kInvalidSocket)
        throw std::runtime_error("UdpMulticastSender::createUnicast: socket() failed");

    sender->dest_ = new SockAddr{};
    std::memset(&sender->dest_->addr, 0, sizeof(sender->dest_->addr));
    sender->dest_->addr.sin_family = AF_INET;
    sender->dest_->addr.sin_port = htons(port);

    if (inet_pton(AF_INET, host.c_str(), &sender->dest_->addr.sin_addr) != 1) {
        struct addrinfo hints{}, *result = nullptr;
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_DGRAM;
        if (getaddrinfo(host.c_str(), nullptr, &hints, &result) != 0 || !result)
            throw std::runtime_error("UdpMulticastSender::createUnicast: cannot resolve " + host);
        sender->dest_->addr.sin_addr =
            reinterpret_cast<struct sockaddr_in*>(result->ai_addr)->sin_addr;
        freeaddrinfo(result);
    }

    return sender;
}

UdpMulticastSender::UdpMulticastSender(const std::string& group, uint16_t port, uint8_t ttl) {
    sock_ = static_cast<decltype(sock_)>(
        socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP));
    if (static_cast<socket_t>(sock_) == kInvalidSocket)
        throw std::runtime_error("UdpMulticastSender: socket() failed");

    int ttl_val = ttl;
    setsockopt(static_cast<socket_t>(sock_), IPPROTO_IP, IP_MULTICAST_TTL,
               reinterpret_cast<const char*>(&ttl_val), sizeof(ttl_val));

    dest_ = new SockAddr{};
    std::memset(&dest_->addr, 0, sizeof(dest_->addr));
    dest_->addr.sin_family = AF_INET;
    dest_->addr.sin_port = htons(port);
    inet_pton(AF_INET, group.c_str(), &dest_->addr.sin_addr);
}

UdpMulticastSender::~UdpMulticastSender() {
    if (static_cast<socket_t>(sock_) != kInvalidSocket)
        closeSocket(static_cast<socket_t>(sock_));
    delete dest_;
}

bool UdpMulticastSender::send(const uint8_t* data, size_t len) {
    auto sent = sendto(
        static_cast<socket_t>(sock_),
        reinterpret_cast<const char*>(data),
        static_cast<int>(len),
        0,
        reinterpret_cast<const struct sockaddr*>(&dest_->addr),
        sizeof(dest_->addr));

    if (sent < 0) {
        std::fprintf(stderr, "UdpMulticastSender: sendto failed\n");
        return false;
    }
    return true;
}

}  // namespace itch
}  // namespace qrsdp
