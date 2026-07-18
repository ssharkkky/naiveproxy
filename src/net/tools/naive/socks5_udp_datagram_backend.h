// Copyright 2026 The NaiveProxy Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_TOOLS_NAIVE_SOCKS5_UDP_DATAGRAM_BACKEND_H_
#define NET_TOOLS_NAIVE_SOCKS5_UDP_DATAGRAM_BACKEND_H_

#include <memory>

#include "base/functional/callback.h"
#include "net/base/completion_once_callback.h"
#include "net/tools/naive/socks5_udp_codec.h"

namespace net {

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

  // Returns OK for synchronous acceptance, ERR_IO_PENDING when |callback|
  // will report OK or a net error, or a net error directly. Positive byte
  // counts are not valid results at this abstraction boundary.
  virtual int Send(Socks5UdpDatagram datagram,
                   CompletionOnceCallback callback) = 0;
};

using Socks5UdpBackendFactory =
    base::RepeatingCallback<std::unique_ptr<Socks5UdpDatagramBackend>()>;

}  // namespace net

#endif  // NET_TOOLS_NAIVE_SOCKS5_UDP_DATAGRAM_BACKEND_H_
