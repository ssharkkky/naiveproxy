// Copyright 2026 The NaiveProxy Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdint.h>

#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "base/containers/span.h"
#include "net/base/host_port_pair.h"
#include "net/tools/naive/socks5_udp_codec.h"

namespace {

int failures = 0;

struct ValidCodecCase {
  const char* description;
  net::Socks5UdpDatagram datagram;
  std::vector<uint8_t> wire;
};

struct RoundTripCase {
  const char* description;
  net::Socks5UdpDatagram datagram;
};

struct ParseErrorCase {
  const char* description;
  std::vector<uint8_t> wire;
  net::Socks5UdpCodecError error;
};

struct BuildErrorCase {
  const char* description;
  net::Socks5UdpDatagram datagram;
  net::Socks5UdpCodecError error;
};

void Expect(bool condition, const char* description) {
  if (!condition) {
    std::cerr << "FAILED: " << description << "\n";
    ++failures;
  }
}

void ExpectRoundTrip(const net::Socks5UdpDatagram& input,
                     const char* description) {
  auto packet = net::BuildSocks5UdpDatagram(input);
  Expect(packet.has_value(), description);
  if (!packet.has_value()) {
    return;
  }
  auto output = net::ParseSocks5UdpDatagram(*packet);
  Expect(output.has_value(), description);
  if (output.has_value()) {
    Expect(*output == input, description);
  }
}

void ExpectParse(const std::vector<uint8_t>& packet,
                 const net::Socks5UdpDatagram& expected,
                 const char* description) {
  auto result = net::ParseSocks5UdpDatagram(packet);
  Expect(result.has_value(), description);
  if (result.has_value()) {
    Expect(*result == expected, description);
  }
}

void ExpectBuild(const net::Socks5UdpDatagram& input,
                 const std::vector<uint8_t>& expected,
                 const char* description) {
  auto result = net::BuildSocks5UdpDatagram(input);
  Expect(result.has_value(), description);
  if (result.has_value()) {
    Expect(*result == expected, description);
  }
}

void ExpectParseError(std::vector<uint8_t> packet,
                      net::Socks5UdpCodecError expected,
                      const char* description) {
  auto result = net::ParseSocks5UdpDatagram(packet);
  Expect(!result.has_value(), description);
  if (!result.has_value()) {
    Expect(result.error() == expected, description);
  }
}

void ExpectBuildError(const net::Socks5UdpDatagram& datagram,
                      net::Socks5UdpCodecError expected,
                      const char* description) {
  auto result = net::BuildSocks5UdpDatagram(datagram);
  Expect(!result.has_value(), description);
  if (!result.has_value()) {
    Expect(result.error() == expected, description);
  }
}

}  // namespace

int main() {
  const std::vector<ValidCodecCase> valid_cases = {
      {"fixed IPv4 wire",
       {{net::Socks5UdpAddressType::kIpv4, "127.0.0.1", 19000},
        {'i', 'p', 'v', '4', 0, 0xff}},
       {0, 0, 0, 1, 127, 0, 0, 1, 0x4a, 0x38, 'i', 'p', 'v', '4', 0,
        0xff}},
      {"fixed IPv6 wire",
       {{net::Socks5UdpAddressType::kIpv6, "2001:db8::1", 443},
        {0, 1, 2, 3}},
       {0,    0, 0, 4, 0x20, 0x01, 0x0d, 0xb8, 0,    0, 0, 0, 0,
        0,    0, 0, 0, 0,    0,    1,    0x01, 0xbb, 0, 1, 2, 3}},
      {"fixed domain wire with empty payload",
       {{net::Socks5UdpAddressType::kDomain, "example.test", 53}, {}},
       {0,   0,   0,   3,   12,  'e', 'x', 'a', 'm', 'p',
        'l', 'e', '.', 't', 'e', 's', 't', 0,   53}},
  };
  for (const auto& test : valid_cases) {
    ExpectParse(test.wire, test.datagram, test.description);
    ExpectBuild(test.datagram, test.wire, test.description);
    ExpectRoundTrip(test.datagram, test.description);
  }

  const std::vector<RoundTripCase> boundary_cases = {
      {"zero port is codec-valid",
       {{net::Socks5UdpAddressType::kIpv4, "0.0.0.0", 0}, {}}},
      {"maximum domain and port",
       {{net::Socks5UdpAddressType::kDomain, std::string(255, 'a'), 65535},
        {0, 0xff}}},
  };
  for (const auto& test : boundary_cases) {
    ExpectRoundTrip(test.datagram, test.description);
  }

  const std::vector<ParseErrorCase> parse_error_cases = {
      {"empty packet", {}, net::Socks5UdpCodecError::kPacketTooShort},
      {"one-byte fixed header", {0},
       net::Socks5UdpCodecError::kPacketTooShort},
      {"two-byte fixed header", {0, 0},
       net::Socks5UdpCodecError::kPacketTooShort},
      {"truncated fixed header", {0, 0, 0},
       net::Socks5UdpCodecError::kPacketTooShort},
      {"nonzero reserved field", {1, 0, 0, 1},
       net::Socks5UdpCodecError::kInvalidReserved},
      {"second reserved byte nonzero", {0, 1, 0, 1},
       net::Socks5UdpCodecError::kInvalidReserved},
      {"unsupported fragment", {0, 0, 1, 1},
       net::Socks5UdpCodecError::kFragmentUnsupported},
      {"maximum fragment identifier unsupported", {0, 0, 255, 1},
       net::Socks5UdpCodecError::kFragmentUnsupported},
      {"unsupported address type", {0, 0, 0, 2},
       net::Socks5UdpCodecError::kAddressTypeUnsupported},
      {"zero address type unsupported", {0, 0, 0, 0},
       net::Socks5UdpCodecError::kAddressTypeUnsupported},
      {"unknown address type unsupported", {0, 0, 0, 5},
       net::Socks5UdpCodecError::kAddressTypeUnsupported},
      {"IPv4 missing port byte", {0, 0, 0, 1, 127, 0, 0, 1, 0},
       net::Socks5UdpCodecError::kPacketTooShort},
      {"truncated IPv6 address", {0, 0, 0, 4, 0, 0, 0, 0},
       net::Socks5UdpCodecError::kPacketTooShort},
      {"IPv6 missing both port bytes",
       {0, 0, 0, 4, 0x20, 0x01, 0x0d, 0xb8, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 1},
       net::Socks5UdpCodecError::kPacketTooShort},
      {"IPv6 missing one port byte",
       {0, 0, 0, 4, 0x20, 0x01, 0x0d, 0xb8, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 1, 0x01},
       net::Socks5UdpCodecError::kPacketTooShort},
      {"domain missing length", {0, 0, 0, 3},
       net::Socks5UdpCodecError::kPacketTooShort},
      {"empty domain", {0, 0, 0, 3, 0, 0, 53},
       net::Socks5UdpCodecError::kEmptyDomain},
      {"truncated domain", {0, 0, 0, 3, 3, 'a', 'b', 0, 53},
       net::Socks5UdpCodecError::kPacketTooShort},
      {"domain missing both port bytes", {0, 0, 0, 3, 3, 'a', 'b', 'c'},
       net::Socks5UdpCodecError::kPacketTooShort},
      {"domain missing one port byte",
       {0, 0, 0, 3, 3, 'a', 'b', 'c', 0},
       net::Socks5UdpCodecError::kPacketTooShort},
      {"domain containing NUL", {0, 0, 0, 3, 3, 'a', 0, 'b', 0, 53},
       net::Socks5UdpCodecError::kInvalidDestination},
  };
  for (const auto& test : parse_error_cases) {
    ExpectParseError(test.wire, test.error, test.description);
  }

  const std::vector<BuildErrorCase> build_error_cases = {
      {"empty destination rejected when building",
       {{net::Socks5UdpAddressType::kDomain, "", 53}, {'x'}},
       net::Socks5UdpCodecError::kInvalidDestination},
      {"overlong domain rejected when building",
       {{net::Socks5UdpAddressType::kDomain, std::string(256, 'a'), 53},
        {'x'}},
       net::Socks5UdpCodecError::kDomainTooLong},
      {"address family mismatch rejected when building",
       {{net::Socks5UdpAddressType::kIpv4, "::1", 53}, {'x'}},
       net::Socks5UdpCodecError::kInvalidDestination},
      {"invalid IPv4 rejected when building",
       {{net::Socks5UdpAddressType::kIpv4, "999.1.1.1", 53}, {'x'}},
       net::Socks5UdpCodecError::kInvalidDestination},
      {"invalid IPv6 rejected when building",
       {{net::Socks5UdpAddressType::kIpv6, "2001:::1", 53}, {'x'}},
       net::Socks5UdpCodecError::kInvalidDestination},
      {"domain containing NUL rejected when building",
       {{net::Socks5UdpAddressType::kDomain, std::string("a\0b", 3), 53},
        {'x'}},
       net::Socks5UdpCodecError::kInvalidDestination},
  };
  for (const auto& test : build_error_cases) {
    ExpectBuildError(test.datagram, test.error, test.description);
  }

  if (failures != 0) {
    std::cerr << "M2 G1 codec failures=" << failures << "\n";
    return 1;
  }
  std::cout << "M2_SOCKS5_UDP_TEST_SKELETON_OK\n";
  std::cout << "M2_G1_CODEC_OK\n";
  return 0;
}
