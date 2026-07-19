// Copyright 2026 The NaiveProxy Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_TOOLS_NAIVE_SOCKS5_UDP_DATAGRAM_BACKEND_H_
#define NET_TOOLS_NAIVE_SOCKS5_UDP_DATAGRAM_BACKEND_H_

#include <stddef.h>

#include <memory>
#include <string>
#include <tuple>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/time/time.h"
#include "net/base/completion_once_callback.h"
#include "net/base/network_anonymization_key.h"
#include "net/base/proxy_chain.h"
#include "net/log/net_log_with_source.h"
#include "net/tools/naive/socks5_udp_codec.h"
#include "net/traffic_annotation/network_traffic_annotation.h"

namespace net {

class HttpNetworkSession;

// Immutable inputs captured at the SOCKS5 UDP ASSOCIATE boundary. In
// particular, `network_anonymization_key` is copied from the pending SOCKS
// handshake; a backend must never manufacture a replacement key.
struct Socks5UdpBackendContext {
  Socks5UdpBackendContext(
      unsigned int association_id,
      HttpNetworkSession* session,
      const ProxyChain& proxy_chain,
      const NetworkAnonymizationKey& network_anonymization_key,
      const NetLogWithSource& net_log,
      const NetworkTrafficAnnotationTag& traffic_annotation,
      base::TimeDelta connect_timeout,
      base::TimeDelta target_idle_timeout);
  Socks5UdpBackendContext(const Socks5UdpBackendContext&);
  Socks5UdpBackendContext(Socks5UdpBackendContext&&);
  ~Socks5UdpBackendContext();

  unsigned int association_id;
  // Non-owning. NaiveProxy and all association backends must be destroyed
  // before the URLRequestContext that owns this session.
  raw_ptr<HttpNetworkSession> session;
  ProxyChain proxy_chain;
  NetworkAnonymizationKey network_anonymization_key;
  NetLogWithSource net_log;
  NetworkTrafficAnnotationTag traffic_annotation;
  base::TimeDelta connect_timeout;
  base::TimeDelta target_idle_timeout;
};

// A target key intentionally includes the SOCKS wire address type. A domain
// and a numerically equivalent IP therefore remain separate CONNECT-UDP
// associations, and responses retain the client's original address framing.
struct Socks5UdpTargetKey {
  explicit Socks5UdpTargetKey(const Socks5UdpEndpoint& endpoint)
      : type(endpoint.type), host(endpoint.host), port(endpoint.port) {}

  bool operator<(const Socks5UdpTargetKey& other) const {
    return std::tie(type, host, port) <
           std::tie(other.type, other.host, other.port);
  }

  bool operator==(const Socks5UdpTargetKey&) const = default;

  Socks5UdpAddressType type;
  std::string host;
  uint16_t port;
};

// M3 v1 resource policy. Admission pressure and target-scoped transport
// failures are observable drops; they are not association-fatal errors.
struct Socks5UdpBackendLimits {
  static constexpr size_t kMaxTargets = 32;
  static constexpr size_t kMaxQueuedDatagramsPerTarget = 16;
  static constexpr size_t kMaxQueuedDatagramsPerAssociation = 128;
  static constexpr size_t kMaxQueuedPayloadBytesPerAssociation = 256 * 1024;
  static constexpr size_t kMaxSynchronousPumpOperations = 32;
  static constexpr size_t kMaxActiveAssociationsPerProxy = 256;
  static constexpr base::TimeDelta kDefaultConnectTimeout = base::Seconds(10);
  static constexpr base::TimeDelta kFailedTargetCooldown = base::Seconds(1);
};

// Association-level datagram backend. M2 injects a test echo backend; M3 will
// implement this interface with target-keyed NaiveConnectUdpTunnel instances.
class Socks5UdpDatagramBackend {
 public:
  using ReceiveCallback =
      base::RepeatingCallback<void(Socks5UdpDatagram datagram)>;

  virtual ~Socks5UdpDatagramBackend() = default;

  // Start installs the receive callback. Destroying the backend must cancel
  // pending work and prevent any later callback invocation.
  virtual void Start(ReceiveCallback receive_callback) = 0;

  // Completion means bounded admission, not remote delivery. Returns OK after
  // a datagram is accepted or intentionally dropped by target/packet policy,
  // ERR_IO_PENDING only while that admission decision is pending, and a
  // negative error only when the entire backend is unusable. Positive byte
  // counts are not valid results at this abstraction boundary. A datagram is
  // never replayed after an ambiguous tunnel write/session failure.
  virtual int Send(Socks5UdpDatagram datagram,
                   CompletionOnceCallback callback) = 0;
};

using Socks5UdpBackendFactory =
    base::RepeatingCallback<std::unique_ptr<Socks5UdpDatagramBackend>(
        Socks5UdpBackendContext context)>;

}  // namespace net

#endif  // NET_TOOLS_NAIVE_SOCKS5_UDP_DATAGRAM_BACKEND_H_
