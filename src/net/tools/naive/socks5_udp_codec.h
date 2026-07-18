// Copyright 2026 The NaiveProxy Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_TOOLS_NAIVE_SOCKS5_UDP_CODEC_H_
#define NET_TOOLS_NAIVE_SOCKS5_UDP_CODEC_H_

#include <stdint.h>

#include <string>
#include <vector>

#include "base/containers/span.h"
#include "base/types/expected.h"
#include "net/base/host_port_pair.h"

namespace net {

enum class Socks5UdpAddressType : uint8_t {
  kIpv4 = 0x01,
  kDomain = 0x03,
  kIpv6 = 0x04,
};

// Errors are intentionally structured so the association layer can account
// for unsupported fragments separately from malformed packets without logging
// destinations or payloads.
enum class Socks5UdpCodecError {
  kPacketTooShort,
  kInvalidReserved,
  kFragmentUnsupported,
  kAddressTypeUnsupported,
  kEmptyDomain,
  kDomainTooLong,
  kInvalidDestination,
};

struct Socks5UdpEndpoint {
  Socks5UdpAddressType type = Socks5UdpAddressType::kDomain;
  std::string host;
  uint16_t port = 0;

  HostPortPair ToHostPortPair() const { return HostPortPair(host, port); }
  bool operator==(const Socks5UdpEndpoint&) const = default;
};

struct Socks5UdpDatagram {
  Socks5UdpEndpoint destination;
  std::vector<uint8_t> payload;

  bool operator==(const Socks5UdpDatagram&) const = default;
};

base::expected<Socks5UdpDatagram, Socks5UdpCodecError>
ParseSocks5UdpDatagram(base::span<const uint8_t> packet);

base::expected<std::vector<uint8_t>, Socks5UdpCodecError>
BuildSocks5UdpDatagram(const Socks5UdpDatagram& datagram);

const char* Socks5UdpCodecErrorToString(Socks5UdpCodecError error);

}  // namespace net

#endif  // NET_TOOLS_NAIVE_SOCKS5_UDP_CODEC_H_
