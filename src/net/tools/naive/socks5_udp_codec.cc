// Copyright 2026 The NaiveProxy Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/tools/naive/socks5_udp_codec.h"

#include <stddef.h>

#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "base/strings/string_view_util.h"
#include "net/base/ip_address.h"

namespace net {
namespace {

constexpr uint8_t kAddressTypeIpv4 = 0x01;
constexpr uint8_t kAddressTypeDomain = 0x03;
constexpr uint8_t kAddressTypeIpv6 = 0x04;
constexpr size_t kFixedHeaderSize = 4;
constexpr size_t kPortSize = 2;

base::unexpected<Socks5UdpCodecError> Error(Socks5UdpCodecError error) {
  return base::unexpected(error);
}

uint16_t ReadPort(base::span<const uint8_t, kPortSize> bytes) {
  return static_cast<uint16_t>((static_cast<uint16_t>(bytes[0]) << 8) |
                               static_cast<uint16_t>(bytes[1]));
}

void AppendPort(uint16_t port, std::vector<uint8_t>* packet) {
  packet->push_back(static_cast<uint8_t>(port >> 8));
  packet->push_back(static_cast<uint8_t>(port & 0xff));
}

}  // namespace

base::expected<Socks5UdpDatagram, Socks5UdpCodecError>
ParseSocks5UdpDatagram(base::span<const uint8_t> packet) {
  if (packet.size() < kFixedHeaderSize) {
    return Error(Socks5UdpCodecError::kPacketTooShort);
  }
  if (packet[0] != 0 || packet[1] != 0) {
    return Error(Socks5UdpCodecError::kInvalidReserved);
  }
  if (packet[2] != 0) {
    return Error(Socks5UdpCodecError::kFragmentUnsupported);
  }

  size_t cursor = kFixedHeaderSize;
  std::string host;
  Socks5UdpAddressType address_type;
  switch (packet[3]) {
    case kAddressTypeIpv4: {
      address_type = Socks5UdpAddressType::kIpv4;
      constexpr size_t kAddressSize = IPAddress::kIPv4AddressSize;
      if (packet.size() < cursor + kAddressSize + kPortSize) {
        return Error(Socks5UdpCodecError::kPacketTooShort);
      }
      host = IPAddress(packet.subspan(cursor).first<kAddressSize>()).ToString();
      cursor += kAddressSize;
      break;
    }
    case kAddressTypeIpv6: {
      address_type = Socks5UdpAddressType::kIpv6;
      constexpr size_t kAddressSize = IPAddress::kIPv6AddressSize;
      if (packet.size() < cursor + kAddressSize + kPortSize) {
        return Error(Socks5UdpCodecError::kPacketTooShort);
      }
      host = IPAddress(packet.subspan(cursor).first<kAddressSize>()).ToString();
      cursor += kAddressSize;
      break;
    }
    case kAddressTypeDomain: {
      address_type = Socks5UdpAddressType::kDomain;
      if (packet.size() < cursor + 1) {
        return Error(Socks5UdpCodecError::kPacketTooShort);
      }
      const size_t domain_size = packet[cursor++];
      if (domain_size == 0) {
        return Error(Socks5UdpCodecError::kEmptyDomain);
      }
      if (packet.size() < cursor + domain_size + kPortSize) {
        return Error(Socks5UdpCodecError::kPacketTooShort);
      }
      const auto domain = packet.subspan(cursor, domain_size);
      if (base::as_string_view(domain).find('\0') != std::string_view::npos) {
        return Error(Socks5UdpCodecError::kInvalidDestination);
      }
      host = std::string(base::as_string_view(domain));
      cursor += domain_size;
      break;
    }
    default:
      return Error(Socks5UdpCodecError::kAddressTypeUnsupported);
  }

  const uint16_t port = ReadPort(packet.subspan(cursor).first<kPortSize>());
  cursor += kPortSize;
  Socks5UdpDatagram datagram;
  datagram.destination = {address_type, std::move(host), port};
  datagram.payload.assign(packet.begin() + cursor, packet.end());
  return datagram;
}

base::expected<std::vector<uint8_t>, Socks5UdpCodecError>
BuildSocks5UdpDatagram(const Socks5UdpDatagram& datagram) {
  const std::string& host = datagram.destination.host;
  if (host.empty() || host.find('\0') != std::string::npos) {
    return Error(Socks5UdpCodecError::kInvalidDestination);
  }

  std::vector<uint8_t> packet = {0, 0, 0};
  switch (datagram.destination.type) {
    case Socks5UdpAddressType::kIpv4:
    case Socks5UdpAddressType::kIpv6: {
      const std::optional<IPAddress> address = IPAddress::FromIPLiteral(host);
      const bool expected_family =
          address.has_value() &&
          ((datagram.destination.type == Socks5UdpAddressType::kIpv4 &&
            address->IsIPv4()) ||
           (datagram.destination.type == Socks5UdpAddressType::kIpv6 &&
            address->IsIPv6()));
      if (!expected_family) {
        return Error(Socks5UdpCodecError::kInvalidDestination);
      }
      packet.push_back(static_cast<uint8_t>(datagram.destination.type));
      packet.insert(packet.end(), address->bytes().begin(),
                    address->bytes().end());
      break;
    }
    case Socks5UdpAddressType::kDomain:
      if (host.size() > 255) {
        return Error(Socks5UdpCodecError::kDomainTooLong);
      }
      packet.push_back(kAddressTypeDomain);
      packet.push_back(static_cast<uint8_t>(host.size()));
      packet.insert(packet.end(), host.begin(), host.end());
      break;
  }

  AppendPort(datagram.destination.port, &packet);
  packet.insert(packet.end(), datagram.payload.begin(), datagram.payload.end());
  return packet;
}

const char* Socks5UdpCodecErrorToString(Socks5UdpCodecError error) {
  switch (error) {
    case Socks5UdpCodecError::kPacketTooShort:
      return "packet_too_short";
    case Socks5UdpCodecError::kInvalidReserved:
      return "invalid_reserved";
    case Socks5UdpCodecError::kFragmentUnsupported:
      return "fragment_unsupported";
    case Socks5UdpCodecError::kAddressTypeUnsupported:
      return "address_type_unsupported";
    case Socks5UdpCodecError::kEmptyDomain:
      return "empty_domain";
    case Socks5UdpCodecError::kDomainTooLong:
      return "domain_too_long";
    case Socks5UdpCodecError::kInvalidDestination:
      return "invalid_destination";
  }
  return "unknown";
}

}  // namespace net
