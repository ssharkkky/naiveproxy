// Copyright 2026 The NaiveProxy Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/tools/naive/naive_connect_udp_backend_factory.h"

#include <utility>

#include "base/functional/bind.h"
#include "net/base/io_buffer.h"
#include "net/base/net_errors.h"
#include "net/socket/datagram_client_socket.h"
#include "net/tools/naive/naive_connect_udp_datagram_backend.h"
#include "net/tools/naive/naive_connect_udp_tunnel.h"

namespace net {
namespace {

bool IsAllQuicProxyChain(const ProxyChain& proxy_chain) {
  if (!proxy_chain.IsValid() || proxy_chain.is_direct()) {
    return false;
  }
  for (const ProxyServer& proxy_server : proxy_chain.proxy_servers()) {
    if (!proxy_server.is_quic()) {
      return false;
    }
  }
  return true;
}

class ProductionConnectUdpTargetTunnel final
    : public NaiveConnectUdpTargetTunnel {
 public:
  ProductionConnectUdpTargetTunnel(
      const Socks5UdpBackendContext& context,
      const Socks5UdpEndpoint& target)
      : traffic_annotation_(context.traffic_annotation),
        tunnel_(std::make_unique<NaiveConnectUdpTunnel>(
            context.session,
            context.proxy_chain,
            context.network_anonymization_key,
            HostPortPair(target.host, target.port),
            context.net_log,
            context.traffic_annotation)) {}

  ~ProductionConnectUdpTargetTunnel() override = default;

  int Start(CompletionOnceCallback callback) override {
    return tunnel_->Start(std::move(callback));
  }

  int Read(IOBuffer* buffer,
           int buffer_length,
           CompletionOnceCallback callback) override {
    if (!tunnel_->IsOpen()) {
      return ERR_SOCKET_NOT_CONNECTED;
    }
    return tunnel_->socket()->Read(buffer, buffer_length, std::move(callback));
  }

  int Write(IOBuffer* buffer,
            int buffer_length,
            CompletionOnceCallback callback) override {
    if (!tunnel_->IsOpen()) {
      return ERR_SOCKET_NOT_CONNECTED;
    }
    return tunnel_->socket()->Write(buffer, buffer_length, std::move(callback),
                                    traffic_annotation_);
  }

  bool IsOpen() const override { return tunnel_->IsOpen(); }

  bool LastReadWasDatagram() const override {
    return tunnel_->LastReadWasDatagram();
  }

  size_t MaxPayloadSize() const override {
    return tunnel_->MaxPayloadSize();
  }

 private:
  const NetworkTrafficAnnotationTag traffic_annotation_;
  std::unique_ptr<NaiveConnectUdpTunnel> tunnel_;
};

std::unique_ptr<NaiveConnectUdpTargetTunnel> CreateProductionTargetTunnel(
    const Socks5UdpBackendContext& context,
    const Socks5UdpEndpoint& target) {
  if (!context.session || !IsAllQuicProxyChain(context.proxy_chain)) {
    return nullptr;
  }
  return std::make_unique<ProductionConnectUdpTargetTunnel>(context, target);
}

}  // namespace

std::unique_ptr<Socks5UdpDatagramBackend>
CreateNaiveConnectUdpDatagramBackend(Socks5UdpBackendContext context) {
  if (!context.session || context.network_anonymization_key.IsEmpty() ||
      !context.network_anonymization_key.IsTransient() ||
      context.connect_timeout <= base::TimeDelta() ||
      context.target_idle_timeout <= base::TimeDelta() ||
      !IsAllQuicProxyChain(context.proxy_chain)) {
    return nullptr;
  }
  return std::make_unique<NaiveConnectUdpDatagramBackend>(
      std::move(context), base::BindRepeating(&CreateProductionTargetTunnel));
}

}  // namespace net
