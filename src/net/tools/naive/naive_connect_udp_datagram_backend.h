// Copyright 2026 The NaiveProxy Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_TOOLS_NAIVE_NAIVE_CONNECT_UDP_DATAGRAM_BACKEND_H_
#define NET_TOOLS_NAIVE_NAIVE_CONNECT_UDP_DATAGRAM_BACKEND_H_

#include <stddef.h>

#include <memory>

#include "base/functional/callback.h"
#include "net/base/completion_once_callback.h"
#include "net/tools/naive/socks5_udp_datagram_backend.h"

namespace net {

class IOBuffer;

// Narrow, fakeable boundary around one fixed-target CONNECT-UDP tunnel. Tests
// script this interface; the production implementation added in M3-G3 wraps
// NaiveConnectUdpTunnel without virtualizing Chromium session-pool internals.
class NaiveConnectUdpTargetTunnel {
 public:
  virtual ~NaiveConnectUdpTargetTunnel() = default;

  virtual int Start(CompletionOnceCallback callback) = 0;
  virtual int Read(IOBuffer* buffer,
                   int buffer_length,
                   CompletionOnceCallback callback) = 0;
  virtual int Write(IOBuffer* buffer,
                    int buffer_length,
                    CompletionOnceCallback callback) = 0;

  // `LastReadWasDatagram()` disambiguates a valid zero-length UDP datagram
  // from a stream-close result of zero.
  virtual bool IsOpen() const = 0;
  virtual bool LastReadWasDatagram() const = 0;
  virtual size_t MaxPayloadSize() const = 0;
};

using NaiveConnectUdpTargetTunnelFactory =
    base::RepeatingCallback<std::unique_ptr<NaiveConnectUdpTargetTunnel>(
        const Socks5UdpBackendContext& context,
        const Socks5UdpEndpoint& target)>;

// Association-level owner that will map SOCKS targets to fixed-target
// CONNECT-UDP tunnels. G0 establishes the immutable context and injectable
// tunnel seam; G1/G2 fill in the state machine and target map.
class NaiveConnectUdpDatagramBackend final
    : public Socks5UdpDatagramBackend {
 public:
  NaiveConnectUdpDatagramBackend(
      Socks5UdpBackendContext context,
      NaiveConnectUdpTargetTunnelFactory tunnel_factory);
  ~NaiveConnectUdpDatagramBackend() override;

  NaiveConnectUdpDatagramBackend(const NaiveConnectUdpDatagramBackend&) =
      delete;
  NaiveConnectUdpDatagramBackend& operator=(
      const NaiveConnectUdpDatagramBackend&) = delete;

  void Start(ReceiveCallback receive_callback) override;
  int Send(Socks5UdpDatagram datagram,
           CompletionOnceCallback callback) override;

 private:
  const Socks5UdpBackendContext context_;
  const NaiveConnectUdpTargetTunnelFactory tunnel_factory_;
  ReceiveCallback receive_callback_;
};

}  // namespace net

#endif  // NET_TOOLS_NAIVE_NAIVE_CONNECT_UDP_DATAGRAM_BACKEND_H_
