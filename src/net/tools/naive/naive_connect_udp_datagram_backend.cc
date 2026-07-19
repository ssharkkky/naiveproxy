// Copyright 2026 The NaiveProxy Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/tools/naive/naive_connect_udp_datagram_backend.h"

#include <utility>

#include "base/check.h"
#include "net/base/net_errors.h"

namespace net {

NaiveConnectUdpDatagramBackend::NaiveConnectUdpDatagramBackend(
    Socks5UdpBackendContext context,
    NaiveConnectUdpTargetTunnelFactory tunnel_factory)
    : context_(std::move(context)),
      tunnel_factory_(std::move(tunnel_factory)) {
  CHECK(context_.session);
  CHECK(tunnel_factory_);
}

NaiveConnectUdpDatagramBackend::~NaiveConnectUdpDatagramBackend() = default;

void NaiveConnectUdpDatagramBackend::Start(
    ReceiveCallback receive_callback) {
  CHECK(!receive_callback_);
  CHECK(receive_callback);
  receive_callback_ = std::move(receive_callback);
}

int NaiveConnectUdpDatagramBackend::Send(
    Socks5UdpDatagram datagram,
    CompletionOnceCallback callback) {
  if (!receive_callback_) {
    return ERR_UNEXPECTED;
  }
  // G0 deliberately does not install this skeleton in production. Until G1
  // supplies the target state machine, treat the packet as an observable
  // policy drop while preserving the backend's admission contract.
  std::unique_ptr<NaiveConnectUdpTargetTunnel> tunnel =
      tunnel_factory_.Run(context_, datagram.destination);
  return tunnel ? OK : ERR_FAILED;
}

}  // namespace net
