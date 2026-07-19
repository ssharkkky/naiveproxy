// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_TOOLS_NAIVE_NAIVE_CONNECT_UDP_TUNNEL_H_
#define NET_TOOLS_NAIVE_NAIVE_CONNECT_UDP_TUNNEL_H_

#include <memory>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "net/base/completion_once_callback.h"
#include "net/base/host_port_pair.h"
#include "net/base/net_error_details.h"
#include "net/base/network_anonymization_key.h"
#include "net/base/proxy_chain.h"
#include "net/log/net_log_with_source.h"
#include "net/traffic_annotation/network_traffic_annotation.h"

namespace net {

class DatagramClientSocket;
class HttpNetworkSession;
class NaiveQuicProxyStreamRequest;
class QuicProxyDatagramClientSocket;

// Establishes one RFC 9298 CONNECT-UDP tunnel to a fixed target through the
// last QUIC proxy in `proxy_chain`.
//
// This class only owns the Chromium client-side tunnel. SOCKS5 UDP framing,
// association routing, queues, idle expiration, and server-side UDP egress are
// intentionally outside this M1 boundary.
class NaiveConnectUdpTunnel {
 public:
  NaiveConnectUdpTunnel(
      HttpNetworkSession* session,
      const ProxyChain& proxy_chain,
      const NetworkAnonymizationKey& network_anonymization_key,
      const HostPortPair& target,
      const NetLogWithSource& net_log,
      const NetworkTrafficAnnotationTag& traffic_annotation);
  ~NaiveConnectUdpTunnel();

  NaiveConnectUdpTunnel(const NaiveConnectUdpTunnel&) = delete;
  NaiveConnectUdpTunnel& operator=(const NaiveConnectUdpTunnel&) = delete;

  // Returns OK or a network error synchronously, or ERR_IO_PENDING and invokes
  // `callback` exactly once when the CONNECT-UDP response has been processed.
  int Start(CompletionOnceCallback callback);

  // Returns the connected datagram socket. May only be called after Start()
  // completes successfully. The returned pointer is owned by this tunnel.
  DatagramClientSocket* socket() const;

  // Live state used by the M3 association backend. These methods preserve the
  // empty-datagram versus EOF distinction and avoid guessing a QUIC MTU.
  bool IsOpen() const;
  bool LastReadWasDatagram() const;
  size_t MaxPayloadSize() const;

  const HostPortPair& target() const { return target_; }
  const NetErrorDetails& net_error_details() const { return net_error_details_; }

 private:
  enum class State {
    kNone,
    kRequestStream,
    kRequestStreamComplete,
    kConnectUdpComplete,
  };

  void OnIOComplete(int result);
  int DoLoop(int last_io_result);
  int DoRequestStream();
  int DoRequestStreamComplete(int result);
  int DoConnectUdpComplete(int result);

  const raw_ptr<HttpNetworkSession> session_;
  const ProxyChain proxy_chain_;
  const NetworkAnonymizationKey network_anonymization_key_;
  const HostPortPair target_;
  const NetLogWithSource net_log_;
  const NetworkTrafficAnnotationTag traffic_annotation_;
  NetErrorDetails net_error_details_;

  State next_state_ = State::kNone;
  CompletionOnceCallback callback_;
  bool connected_ = false;

  // Declaration order is intentional: the datagram socket must be destroyed
  // before the stream request releases its retained QUIC session handle.
  std::unique_ptr<NaiveQuicProxyStreamRequest> stream_request_;
  std::unique_ptr<QuicProxyDatagramClientSocket> socket_;

  base::WeakPtrFactory<NaiveConnectUdpTunnel> weak_ptr_factory_{this};
};

}  // namespace net

#endif  // NET_TOOLS_NAIVE_NAIVE_CONNECT_UDP_TUNNEL_H_
