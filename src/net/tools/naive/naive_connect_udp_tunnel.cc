// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/tools/naive/naive_connect_udp_tunnel.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/memory/ref_counted.h"
#include "net/base/ip_endpoint.h"
#include "net/base/net_errors.h"
#include "net/http/http_auth_controller.h"
#include "net/http/http_network_session.h"
#include "net/quic/quic_proxy_datagram_client_socket.h"
#include "net/socket/datagram_client_socket.h"
#include "net/tools/naive/naive_quic_proxy_stream_request.h"

namespace net {

NaiveConnectUdpTunnel::NaiveConnectUdpTunnel(
    HttpNetworkSession* session,
    const ProxyChain& proxy_chain,
    const NetworkAnonymizationKey& network_anonymization_key,
    const HostPortPair& target,
    const NetLogWithSource& net_log,
    const NetworkTrafficAnnotationTag& traffic_annotation)
    : session_(session),
      proxy_chain_(proxy_chain),
      network_anonymization_key_(network_anonymization_key),
      target_(target),
      net_log_(net_log),
      traffic_annotation_(traffic_annotation) {
  CHECK(session_);
}

NaiveConnectUdpTunnel::~NaiveConnectUdpTunnel() = default;

int NaiveConnectUdpTunnel::Start(CompletionOnceCallback callback) {
  CHECK_EQ(next_state_, State::kNone);
  CHECK(!stream_request_);
  CHECK(!socket_);
  CHECK(!connected_);
  CHECK(callback_.is_null());

  callback_ = std::move(callback);
  next_state_ = State::kRequestStream;
  int result = DoLoop(OK);
  if (result != ERR_IO_PENDING) {
    callback_.Reset();
  }
  return result;
}

DatagramClientSocket* NaiveConnectUdpTunnel::socket() const {
  CHECK(connected_);
  CHECK(socket_);
  return socket_.get();
}

void NaiveConnectUdpTunnel::OnIOComplete(int result) {
  result = DoLoop(result);
  if (result != ERR_IO_PENDING && !callback_.is_null()) {
    std::move(callback_).Run(result);
  }
}

int NaiveConnectUdpTunnel::DoLoop(int last_io_result) {
  int result = last_io_result;
  do {
    State state = next_state_;
    next_state_ = State::kNone;
    switch (state) {
      case State::kRequestStream:
        result = DoRequestStream();
        break;
      case State::kRequestStreamComplete:
        result = DoRequestStreamComplete(result);
        break;
      case State::kConnectUdpComplete:
        result = DoConnectUdpComplete(result);
        break;
      case State::kNone:
        return ERR_UNEXPECTED;
    }
  } while (result != ERR_IO_PENDING && next_state_ != State::kNone);
  return result;
}

int NaiveConnectUdpTunnel::DoRequestStream() {
  if (target_.host().empty() || target_.port() == 0) {
    return ERR_ADDRESS_INVALID;
  }

  next_state_ = State::kRequestStreamComplete;
  stream_request_ = std::make_unique<NaiveQuicProxyStreamRequest>(
      session_, proxy_chain_, network_anonymization_key_, net_log_,
      traffic_annotation_);
  return stream_request_->Start(
      base::BindOnce(&NaiveConnectUdpTunnel::OnIOComplete,
                     weak_ptr_factory_.GetWeakPtr()));
}

int NaiveConnectUdpTunnel::DoRequestStreamComplete(int result) {
  if (result < 0) {
    net_error_details_ = stream_request_->net_error_details();
    stream_request_.reset();
    return result;
  }

  IPEndPoint local_address;
  result = stream_request_->GetLocalAddress(&local_address);
  if (result < 0) {
    stream_request_.reset();
    return result;
  }

  IPEndPoint proxy_peer_address;
  result = stream_request_->GetPeerAddress(&proxy_peer_address);
  if (result < 0) {
    stream_request_.reset();
    return result;
  }

  const GURL url = QuicProxyDatagramClientSocket::BuildConnectUdpUrl(
      proxy_chain_.Last(), target_);
  auto auth_controller = base::MakeRefCounted<HttpAuthController>(
      HttpAuth::AUTH_PROXY,
      GURL("https://" + proxy_chain_.Last().host_port_pair().ToString()),
      network_anonymization_key_, session_->http_auth_cache(),
      session_->http_auth_handler_factory(), session_->host_resolver());
  socket_ = std::make_unique<QuicProxyDatagramClientSocket>(
      url, proxy_chain_, stream_request_->GetUserAgent(), net_log_,
      std::move(auth_controller), session_->context().proxy_delegate);

  next_state_ = State::kConnectUdpComplete;
  return socket_->ConnectViaStream(
      local_address, proxy_peer_address, stream_request_->ReleaseStream(),
      base::BindOnce(&NaiveConnectUdpTunnel::OnIOComplete,
                     weak_ptr_factory_.GetWeakPtr()));
}

int NaiveConnectUdpTunnel::DoConnectUdpComplete(int result) {
  if (result < 0) {
    socket_.reset();
    stream_request_.reset();
    return result;
  }
  connected_ = true;
  return OK;
}

}  // namespace net
