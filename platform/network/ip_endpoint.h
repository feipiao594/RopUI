#ifndef _ROP_PLATFORM_NETWORK_IP_ENDPOINT_H
#define _ROP_PLATFORM_NETWORK_IP_ENDPOINT_H

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

namespace RopHive::Network {

// Platform-neutral IP endpoint (no DNS).

using Ipv4Bytes = std::array<uint8_t, 4>;
using Ipv6Bytes = std::array<uint8_t, 16>;

struct IpEndpointV4 {
    Ipv4Bytes ip{};
    uint16_t port{0}; // host byte order
};

struct IpEndpointV6 {
    Ipv6Bytes ip{};
    uint16_t port{0};     // host byte order
    uint32_t scope_id{0}; // IPv6 zone index (0 means none/unspecified)
};

// Tagged union endpoint.
using IpEndpoint = std::variant<IpEndpointV4, IpEndpointV6>;

// ---- Constructors ----

IpEndpoint makeIpEndpointV4(Ipv4Bytes ip, uint16_t port = 0);
IpEndpoint makeIpEndpointV6(Ipv6Bytes ip, uint16_t port = 0, uint32_t scope_id = 0);

// ---- Accessors / formatting ----

bool isV4(const IpEndpoint& ep) noexcept;
bool isV6(const IpEndpoint& ep) noexcept;

uint16_t portOf(const IpEndpoint& ep) noexcept;
void setPort(IpEndpoint& ep, uint16_t port) noexcept;

// IP only (no port). For IPv6, includes "%<scope_id>" suffix when scope_id != 0.
std::string ipToString(const IpEndpoint& ep);

// "IP:Port" for IPv4; "[IP%scope]:Port" for IPv6.
std::string ipPortToString(const IpEndpoint& ep);
std::string ipPortToString(const std::optional<IpEndpoint>& ep);

// ---- Parsing ----
// Supports:
// - IPv4: "1.2.3.4" or "1.2.3.4:80"
// - IPv6: "2001:db8::1" (port defaults to 0)
// - IPv6 with port: "[2001:db8::1]:443"
// - IPv6 scope_id numeric: "fe80::1%3" or "[fe80::1%3]:80"
std::optional<IpEndpoint> parseIpEndpoint(std::string_view text);

} // namespace RopHive::Network

#endif // _ROP_PLATFORM_NETWORK_IP_ENDPOINT_H

