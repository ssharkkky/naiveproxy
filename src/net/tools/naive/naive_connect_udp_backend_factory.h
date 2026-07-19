// Copyright 2026 The NaiveProxy Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_TOOLS_NAIVE_NAIVE_CONNECT_UDP_BACKEND_FACTORY_H_
#define NET_TOOLS_NAIVE_NAIVE_CONNECT_UDP_BACKEND_FACTORY_H_

#include <memory>

#include "net/tools/naive/socks5_udp_datagram_backend.h"

namespace net {

// Creates the production RFC 9298/H3 DATAGRAM backend. Invalid, direct, or
// mixed/non-QUIC proxy chains are rejected defensively even though NaiveProxy
// also performs the eligibility check before the SOCKS success reply.
std::unique_ptr<Socks5UdpDatagramBackend>
CreateNaiveConnectUdpDatagramBackend(Socks5UdpBackendContext context);

}  // namespace net

#endif  // NET_TOOLS_NAIVE_NAIVE_CONNECT_UDP_BACKEND_FACTORY_H_
