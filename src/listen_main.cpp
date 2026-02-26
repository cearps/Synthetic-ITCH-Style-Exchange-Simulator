#include "itch/itch_messages.h"
#include "itch/endian.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
    #endif
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
    using socket_t = SOCKET;
#else
    #include <arpa/inet.h>
    #include <netinet/in.h>
    #include <sys/socket.h>
    #include <unistd.h>
    using socket_t = int;
#endif

static void printUsage(const char* prog) {
    std::fprintf(stderr,
        "Usage: %s [options]\n"
        "  --multicast-group <s> Multicast address (default: 239.1.1.1)\n"
        "  --port <n>            UDP port (default: 5001)\n"
        "  --no-multicast        Skip multicast group join (for unicast reception)\n"
        "  --help                Show this help\n",
        prog);
}

static void decodeItchMessage(const uint8_t* data, size_t len, uint64_t seq) {
    using namespace qrsdp::itch;

    if (len < 1) return;
    char msg_type = static_cast<char>(data[0]);

    switch (msg_type) {
    case kMsgTypeSystemEvent: {
        if (len < sizeof(SystemEventMsg)) return;
        auto* msg = reinterpret_cast<const SystemEventMsg*>(data);
        uint64_t ts = load48be(msg->timestamp);
        std::printf("[seq=%llu] SYSTEM_EVENT code=%c ts=%llu\n",
                    static_cast<unsigned long long>(seq),
                    msg->event_code,
                    static_cast<unsigned long long>(ts));
        break;
    }
    case kMsgTypeStockDirectory: {
        if (len < sizeof(StockDirectoryMsg)) return;
        auto* msg = reinterpret_cast<const StockDirectoryMsg*>(data);
        char stock[9] = {};
        std::memcpy(stock, msg->stock, 8);
        uint64_t ts = load48be(msg->timestamp);
        std::printf("[seq=%llu] STOCK_DIRECTORY stock=%.8s locate=%u ts=%llu\n",
                    static_cast<unsigned long long>(seq),
                    stock,
                    betoh16(msg->stock_locate),
                    static_cast<unsigned long long>(ts));
        break;
    }
    case kMsgTypeAddOrder: {
        if (len < sizeof(AddOrderMsg)) return;
        auto* msg = reinterpret_cast<const AddOrderMsg*>(data);
        char stock[9] = {};
        std::memcpy(stock, msg->stock, 8);
        uint64_t ts = load48be(msg->timestamp);
        uint32_t price = betoh32(msg->price);
        std::printf("[seq=%llu] ADD_ORDER ref=%llu side=%c shares=%u stock=%.8s price=%u.%04u ts=%llu\n",
                    static_cast<unsigned long long>(seq),
                    static_cast<unsigned long long>(betoh64(msg->order_reference)),
                    msg->buy_sell,
                    betoh32(msg->shares),
                    stock,
                    price / 10000, price % 10000,
                    static_cast<unsigned long long>(ts));
        break;
    }
    case kMsgTypeOrderDelete: {
        if (len < sizeof(OrderDeleteMsg)) return;
        auto* msg = reinterpret_cast<const OrderDeleteMsg*>(data);
        uint64_t ts = load48be(msg->timestamp);
        std::printf("[seq=%llu] ORDER_DELETE ref=%llu ts=%llu\n",
                    static_cast<unsigned long long>(seq),
                    static_cast<unsigned long long>(betoh64(msg->order_reference)),
                    static_cast<unsigned long long>(ts));
        break;
    }
    case kMsgTypeOrderExecuted: {
        if (len < sizeof(OrderExecutedMsg)) return;
        auto* msg = reinterpret_cast<const OrderExecutedMsg*>(data);
        uint64_t ts = load48be(msg->timestamp);
        std::printf("[seq=%llu] ORDER_EXECUTED ref=%llu shares=%u match=%llu ts=%llu\n",
                    static_cast<unsigned long long>(seq),
                    static_cast<unsigned long long>(betoh64(msg->order_reference)),
                    betoh32(msg->executed_shares),
                    static_cast<unsigned long long>(betoh64(msg->match_number)),
                    static_cast<unsigned long long>(ts));
        break;
    }
    default:
        std::printf("[seq=%llu] UNKNOWN type=%c len=%zu\n",
                    static_cast<unsigned long long>(seq),
                    msg_type,
                    len);
        break;
    }
}

int main(int argc, char* argv[]) {
    std::string group = "239.1.1.1";
    uint16_t port = 5001;
    bool join_multicast = true;

    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        auto next = [&]() -> const char* {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "missing value for %s\n", arg);
                std::exit(1);
            }
            return argv[++i];
        };

        if (std::strcmp(arg, "--multicast-group") == 0) group = next();
        else if (std::strcmp(arg, "--port") == 0) port = static_cast<uint16_t>(std::atoi(next()));
        else if (std::strcmp(arg, "--no-multicast") == 0) join_multicast = false;
        else if (std::strcmp(arg, "--help") == 0 || std::strcmp(arg, "-h") == 0) {
            printUsage(argv[0]);
            return 0;
        } else {
            std::fprintf(stderr, "unknown argument: %s\n", arg);
            printUsage(argv[0]);
            return 1;
        }
    }

#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif

    socket_t sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

    int reuse = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char*>(&reuse), sizeof(reuse));

    struct sockaddr_in bind_addr{};
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_port = htons(port);
    bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(sock, reinterpret_cast<struct sockaddr*>(&bind_addr), sizeof(bind_addr)) < 0) {
        std::fprintf(stderr, "bind failed on port %u\n", port);
        return 1;
    }

    if (join_multicast) {
        struct ip_mreq mreq{};
        inet_pton(AF_INET, group.c_str(), &mreq.imr_multiaddr);
        mreq.imr_interface.s_addr = htonl(INADDR_ANY);
        setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP,
                   reinterpret_cast<const char*>(&mreq), sizeof(mreq));
    }

    std::printf("=== qrsdp_listen ===\n");
    if (join_multicast)
        std::printf("Listening on multicast %s:%u\n", group.c_str(), port);
    else
        std::printf("Listening on unicast 0.0.0.0:%u\n", port);

    uint8_t buf[2048];
    using namespace qrsdp::itch;

    while (true) {
        auto n = recv(sock, reinterpret_cast<char*>(buf), sizeof(buf), 0);
        if (n < static_cast<int>(kMoldUDP64HeaderSize))
            continue;

        MoldUDP64Header hdr;
        std::memcpy(&hdr, buf, kMoldUDP64HeaderSize);
        uint64_t seq = betoh64(hdr.sequence_number);
        uint16_t count = betoh16(hdr.message_count);

        size_t offset = kMoldUDP64HeaderSize;
        for (uint16_t i = 0; i < count; ++i) {
            if (offset + 2 > static_cast<size_t>(n))
                break;
            uint16_t msg_len_be;
            std::memcpy(&msg_len_be, buf + offset, 2);
            uint16_t msg_len = betoh16(msg_len_be);
            offset += 2;

            if (offset + msg_len > static_cast<size_t>(n))
                break;

            decodeItchMessage(buf + offset, msg_len, seq + i);
            offset += msg_len;
        }
    }

#ifdef _WIN32
    closesocket(sock);
    WSACleanup();
#else
    close(sock);
#endif
    return 0;
}
