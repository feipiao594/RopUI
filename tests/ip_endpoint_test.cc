#include "test_util.h"

#include <platform/network/ip_endpoint.h>

#ifdef _WIN32
    #include <winsock2.h>
#endif

#include <array>
#include <optional>
#include <string>

using namespace RopHive::Network;

static void test_parse_ipv4() {
    auto ep = parseIpEndpoint("127.0.0.1");
    ROPUI_TEST_ASSERT(ep.has_value());
    ROPUI_TEST_ASSERT(isV4(*ep));
    ROPUI_TEST_ASSERT(portOf(*ep) == 0);
    const auto& v4 = std::get<IpEndpointV4>(*ep);
    ROPUI_TEST_ASSERT(v4.ip == (Ipv4Bytes{127, 0, 0, 1}));
    ROPUI_TEST_ASSERT(ipToString(*ep) == "127.0.0.1");
    ROPUI_TEST_ASSERT(ipPortToString(*ep) == "127.0.0.1:0");
}

static void test_parse_ipv4_with_port() {
    auto ep = parseIpEndpoint("127.0.0.1:8080");
    ROPUI_TEST_ASSERT(ep.has_value());
    ROPUI_TEST_ASSERT(isV4(*ep));
    ROPUI_TEST_ASSERT(portOf(*ep) == 8080);
    ROPUI_TEST_ASSERT(ipPortToString(*ep) == "127.0.0.1:8080");
}

static void test_parse_ipv6() {
    auto ep = parseIpEndpoint("::1");
    ROPUI_TEST_ASSERT(ep.has_value());
    ROPUI_TEST_ASSERT(isV6(*ep));
    ROPUI_TEST_ASSERT(portOf(*ep) == 0);

    const auto& v6 = std::get<IpEndpointV6>(*ep);
    Ipv6Bytes expect{};
    expect.fill(0);
    expect[15] = 1;
    ROPUI_TEST_ASSERT(v6.ip == expect);

    ROPUI_TEST_ASSERT(ipToString(*ep) == "::1");
    ROPUI_TEST_ASSERT(ipPortToString(*ep) == "[::1]:0");
}

static void test_parse_ipv6_with_port_bracketed() {
    auto ep = parseIpEndpoint("[::1]:443");
    ROPUI_TEST_ASSERT(ep.has_value());
    ROPUI_TEST_ASSERT(isV6(*ep));
    ROPUI_TEST_ASSERT(portOf(*ep) == 443);
    ROPUI_TEST_ASSERT(ipPortToString(*ep) == "[::1]:443");
}

static void test_parse_ipv6_scope_id_numeric() {
    auto ep = parseIpEndpoint("fe80::1%3");
    ROPUI_TEST_ASSERT(ep.has_value());
    ROPUI_TEST_ASSERT(isV6(*ep));
    const auto& v6 = std::get<IpEndpointV6>(*ep);
    ROPUI_TEST_ASSERT(v6.scope_id == 3);
    ROPUI_TEST_ASSERT(ipToString(*ep).find("%3") != std::string::npos);

    auto round = parseIpEndpoint(ipPortToString(*ep));
    ROPUI_TEST_ASSERT(round.has_value());
    const auto& v6r = std::get<IpEndpointV6>(*round);
    ROPUI_TEST_ASSERT(v6r.scope_id == 3);
}

static void test_make_and_set_port() {
    IpEndpoint ep = makeIpEndpointV4(Ipv4Bytes{1, 2, 3, 4}, 7);
    ROPUI_TEST_ASSERT(isV4(ep));
    ROPUI_TEST_ASSERT(ipPortToString(ep) == "1.2.3.4:7");

    setPort(ep, 9);
    ROPUI_TEST_ASSERT(portOf(ep) == 9);
    ROPUI_TEST_ASSERT(ipPortToString(ep) == "1.2.3.4:9");
}

static void test_invalid_inputs() {
    ROPUI_TEST_ASSERT(!parseIpEndpoint("").has_value());
    ROPUI_TEST_ASSERT(!parseIpEndpoint("127.0.0.1:").has_value());
    ROPUI_TEST_ASSERT(!parseIpEndpoint("127.0.0.1:99999").has_value());
    ROPUI_TEST_ASSERT(!parseIpEndpoint("[::1").has_value());
    ROPUI_TEST_ASSERT(!parseIpEndpoint("[::1]:").has_value());
    ROPUI_TEST_ASSERT(!parseIpEndpoint("fe80::1%notnum").has_value());
}

int main() {
#ifdef _WIN32
    WSADATA wsa{};
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif

    RopUI::Tests::run("ip_endpoint.parse_ipv4", test_parse_ipv4);
    RopUI::Tests::run("ip_endpoint.parse_ipv4_with_port", test_parse_ipv4_with_port);
    RopUI::Tests::run("ip_endpoint.parse_ipv6", test_parse_ipv6);
    RopUI::Tests::run("ip_endpoint.parse_ipv6_bracket_port", test_parse_ipv6_with_port_bracketed);
    RopUI::Tests::run("ip_endpoint.parse_ipv6_scope_id", test_parse_ipv6_scope_id_numeric);
    RopUI::Tests::run("ip_endpoint.make_set_port", test_make_and_set_port);
    RopUI::Tests::run("ip_endpoint.invalid", test_invalid_inputs);

#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}

