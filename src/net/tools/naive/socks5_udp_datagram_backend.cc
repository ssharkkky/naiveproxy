// Copyright 2026 The NaiveProxy Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/tools/naive/socks5_udp_datagram_backend.h"

namespace net {

Socks5UdpBackendContext::Socks5UdpBackendContext(
    unsigned int association_id,
    HttpNetworkSession* session,
    const ProxyChain& proxy_chain,
    const NetworkAnonymizationKey& network_anonymization_key,
    const NetLogWithSource& net_log,
    const NetworkTrafficAnnotationTag& traffic_annotation,
    base::TimeDelta connect_timeout,
    base::TimeDelta target_idle_timeout)
    : association_id(association_id),
      session(session),
      proxy_chain(proxy_chain),
      network_anonymization_key(network_anonymization_key),
      net_log(net_log),
      traffic_annotation(traffic_annotation),
      connect_timeout(connect_timeout),
      target_idle_timeout(target_idle_timeout) {}

Socks5UdpBackendContext::Socks5UdpBackendContext(
    const Socks5UdpBackendContext&) = default;
Socks5UdpBackendContext::Socks5UdpBackendContext(
    Socks5UdpBackendContext&&) = default;
Socks5UdpBackendContext::~Socks5UdpBackendContext() = default;

}  // namespace net
