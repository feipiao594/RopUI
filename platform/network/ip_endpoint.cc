#include "ip_endpoint.h"

#include <charconv>
#include <cstring>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#endif

namespace RopHive::Network {
namespace {

static std::optional<uint16_t> parsePort(std::string_view s) {
  if (s.empty())
    return std::nullopt;
  uint32_t v = 0;
  const char *b = s.data();
  const char *e = s.data() + s.size();
  auto [p, ec] = std::from_chars(b, e, v);
  if (ec != std::errc{} || p != e || v > 65535)
    return std::nullopt;
  return static_cast<uint16_t>(v);
}

static bool parseScopeIdNumeric(std::string_view s, uint32_t &out) {
  if (s.empty())
    return false;
  uint32_t v = 0;
  auto [p, ec] = 
    std::from_chars(s.data(), s.data() + s.size(), v);
  if (ec != std::errc{} || p != s.data() + s.size())
    return false;
  out = v;
  return true;
}

static std::optional<IpEndpointV6>
parseIpv6WithOptionalScope(std::string_view ip_text, uint16_t port) {
  uint32_t scope_id = 0;
  const auto pct = ip_text.find('%');
  std::string_view addr_part = ip_text;
  if (pct != std::string_view::npos) {
    addr_part = ip_text.substr(0, pct);
    std::string_view scope_part = ip_text.substr(pct + 1);
    // Only numeric scope_id is supported at this layer.
    if (!parseScopeIdNumeric(scope_part, scope_id)) {
      return std::nullopt;
    }
  }

  in6_addr a6{};
#ifdef _WIN32
  if (::InetPtonA(AF_INET6, std::string(addr_part).c_str(), &a6) != 1) {
    return std::nullopt;
  }
#else
  if (::inet_pton(AF_INET6, std::string(addr_part).c_str(), &a6) != 1) {
    return std::nullopt;
  }
#endif

  IpEndpointV6 v6;
  std::memcpy(v6.ip.data(), &a6, v6.ip.size());
  v6.port = port;
  v6.scope_id = scope_id;
  return v6;
}

static std::optional<IpEndpointV4> parseIpv4(std::string_view ip_text,
                                             uint16_t port) {
  in_addr a4{};
#ifdef _WIN32
  if (::InetPtonA(AF_INET, std::string(ip_text).c_str(), &a4) != 1) {
    return std::nullopt;
  }
#else
  if (::inet_pton(AF_INET, std::string(ip_text).c_str(), &a4) != 1) {
    return std::nullopt;
  }
#endif
  IpEndpointV4 v4;
  std::memcpy(v4.ip.data(), &a4, v4.ip.size());
  v4.port = port;
  return v4;
}

} // namespace

IpEndpoint makeIpEndpointV4(Ipv4Bytes ip, uint16_t port) {
  return IpEndpointV4{ip, port};
}

IpEndpoint makeIpEndpointV6(Ipv6Bytes ip, uint16_t port, uint32_t scope_id) {
  return IpEndpointV6{ip, port, scope_id};
}

bool isV4(const IpEndpoint &ep) noexcept {
  return std::holds_alternative<IpEndpointV4>(ep);
}
bool isV6(const IpEndpoint &ep) noexcept {
  return std::holds_alternative<IpEndpointV6>(ep);
}

uint16_t portOf(const IpEndpoint &ep) noexcept {
  if (const auto *v4 = std::get_if<IpEndpointV4>(&ep))
    return v4->port;
  return std::get<IpEndpointV6>(ep).port;
}

void setPort(IpEndpoint &ep, uint16_t port) noexcept {
  if (auto *v4 = std::get_if<IpEndpointV4>(&ep)) {
    v4->port = port;
    return;
  }
  std::get<IpEndpointV6>(ep).port = port;
}

std::string ipToString(const IpEndpoint &ep) {
  if (const auto *v4 = std::get_if<IpEndpointV4>(&ep)) {
    in_addr a{};
    std::memcpy(&a, v4->ip.data(), v4->ip.size());
    char buf[INET_ADDRSTRLEN]{};
#ifdef _WIN32
    if (!::InetNtopA(AF_INET, &a, buf, static_cast<DWORD>(sizeof(buf))))
      return "invalid";
#else
    if (!::inet_ntop(AF_INET, &a, buf, sizeof(buf)))
      return "invalid";
#endif
    return std::string(buf);
  }

  const auto &v6 = std::get<IpEndpointV6>(ep);
  in6_addr a{};
  std::memcpy(&a, v6.ip.data(), v6.ip.size());
  char buf[INET6_ADDRSTRLEN]{};
#ifdef _WIN32
  if (!::InetNtopA(AF_INET6, &a, buf, static_cast<DWORD>(sizeof(buf))))
    return "invalid";
#else
  if (!::inet_ntop(AF_INET6, &a, buf, sizeof(buf)))
    return "invalid";
#endif
  std::string s(buf);
  if (v6.scope_id != 0) {
    s.push_back('%');
    s += std::to_string(v6.scope_id);
  }
  return s;
}

std::string ipPortToString(const IpEndpoint &ep) {
  if (const auto *v4 = std::get_if<IpEndpointV4>(&ep)) {
    return ipToString(ep) + ":" + std::to_string(v4->port);
  }
  const auto &v6 = std::get<IpEndpointV6>(ep);
  return "[" + ipToString(ep) + "]:" + std::to_string(v6.port);
}

std::string ipPortToString(const std::optional<IpEndpoint> &ep) {
  if (!ep.has_value())
    return "empty";
  return ipPortToString(*ep);
}

std::optional<IpEndpoint> parseIpEndpoint(std::string_view text) {
  while (!text.empty() && (text.front() == ' ' || text.front() == '\t' ||
                           text.front() == '\n' || text.front() == '\r')) {
    text.remove_prefix(1);
  }
  while (!text.empty() && (text.back() == ' ' || text.back() == '\t' ||
                           text.back() == '\n' || text.back() == '\r')) {
    text.remove_suffix(1);
  }
  if (text.empty())
    return std::nullopt;

  // Bracketed IPv6: [addr%scope]:port?
  if (text.front() == '[') {
    const auto rbr = text.find(']');
    if (rbr == std::string_view::npos)
      return std::nullopt;
    const auto inside = text.substr(1, rbr - 1);
    uint16_t port = 0;
    const auto rest = text.substr(rbr + 1);
    if (!rest.empty()) {
      if (rest.front() != ':')
        return std::nullopt;
      auto p = parsePort(rest.substr(1));
      if (!p.has_value())
        return std::nullopt;
      port = *p;
    }
    auto v6 = parseIpv6WithOptionalScope(inside, port);
    if (!v6.has_value())
      return std::nullopt;
    return IpEndpoint{*v6};
  }

  // Heuristic: IPv4 contains '.' and can be parsed with optional ":port".
  if (text.find('.') != std::string_view::npos) {
    const auto colon = text.rfind(':');
    if (colon != std::string_view::npos) {
      auto ip_part = text.substr(0, colon);
      auto port_part = text.substr(colon + 1);
      auto p = parsePort(port_part);
      if (!p.has_value())
        return std::nullopt;
      auto v4 = parseIpv4(ip_part, *p);
      if (!v4.has_value())
        return std::nullopt;
      return IpEndpoint{*v4};
    }
    auto v4 = parseIpv4(text, 0);
    if (!v4.has_value())
      return std::nullopt;
    return IpEndpoint{*v4};
  }

  // IPv6 without port (since ":" is ambiguous without brackets).
  auto v6 = parseIpv6WithOptionalScope(text, 0);
  if (!v6.has_value())
    return std::nullopt;
  return IpEndpoint{*v6};
}

} // namespace RopHive::Network
