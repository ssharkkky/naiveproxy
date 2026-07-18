// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/tools/naive/naive_quic_proxy_stream_request.h"

#include <optional>
#include <utility>

#include "base/functional/bind.h"
#include "net/base/http_user_agent_settings.h"
#include "net/base/net_errors.h"
#include "net/base/privacy_mode.h"
#include "net/base/request_priority.h"
#include "net/base/session_usage.h"
#include "net/cert/cert_verifier.h"
#include "net/dns/public/secure_dns_policy.h"
#include "net/http/http_network_session.h"
#include "net/quic/quic_context.h"
#include "net/quic/quic_http_utils.h"
#include "net/quic/quic_session_pool.h"
#include "net/socket/socket_tag.h"
#include "net/spdy/multiplexed_session_creation_initiator.h"
#include "url/gurl.h"
#include "url/scheme_host_port.h"

namespace net {

NaiveQuicProxyStreamRequest::NaiveQuicProxyStreamRequest(
    HttpNetworkSession* session,
    const ProxyChain& proxy_chain,
    const NetworkAnonymizationKey& network_anonymization_key,
    const NetLogWithSource& net_log,
    const NetworkTrafficAnnotationTag& traffic_annotation)
    : session_(session),
      proxy_chain_(proxy_chain),
      network_anonymization_key_(network_anonymization_key),
      net_log_(net_log),
      traffic_annotation_(traffic_annotation) {
  CHECK(session_);
}

NaiveQuicProxyStreamRequest::~NaiveQuicProxyStreamRequest() = default;

int NaiveQuicProxyStreamRequest::Start(CompletionOnceCallback callback) {
  CHECK_EQ(next_state_, State::kNone);
  CHECK(!session_request_);
  CHECK(!session_handle_);
  CHECK(!stream_);
  CHECK(callback_.is_null());

  callback_ = std::move(callback);
  next_state_ = State::kCreateSession;
  int result = DoLoop(OK);
  if (result != ERR_IO_PENDING) {
    callback_.Reset();
  }
  return result;
}

std::unique_ptr<QuicChromiumClientStream::Handle>
NaiveQuicProxyStreamRequest::ReleaseStream() {
  CHECK_EQ(next_state_, State::kNone);
  CHECK(stream_);
  return std::move(stream_);
}

int NaiveQuicProxyStreamRequest::GetLocalAddress(IPEndPoint* address) const {
  CHECK(session_handle_);
  return session_handle_->GetSelfAddress(address);
}

int NaiveQuicProxyStreamRequest::GetPeerAddress(IPEndPoint* address) const {
  CHECK(session_handle_);
  return session_handle_->GetPeerAddress(address);
}

std::string NaiveQuicProxyStreamRequest::GetUserAgent() const {
  const HttpUserAgentSettings* settings =
      session_->context().http_user_agent_settings;
  return settings ? settings->GetUserAgent() : std::string();
}

void NaiveQuicProxyStreamRequest::OnIOComplete(int result) {
  result = DoLoop(result);
  if (result != ERR_IO_PENDING && !callback_.is_null()) {
    std::move(callback_).Run(result);
  }
}

int NaiveQuicProxyStreamRequest::DoLoop(int last_io_result) {
  int result = last_io_result;
  do {
    State state = next_state_;
    next_state_ = State::kNone;
    switch (state) {
      case State::kCreateSession:
        result = DoCreateSession();
        break;
      case State::kCreateSessionComplete:
        result = DoCreateSessionComplete(result);
        break;
      case State::kCreateStreamComplete:
        result = DoCreateStreamComplete(result);
        break;
      case State::kNone:
        return ERR_UNEXPECTED;
    }
  } while (result != ERR_IO_PENDING && next_state_ != State::kNone);
  return result;
}

int NaiveQuicProxyStreamRequest::DoCreateSession() {
  if (proxy_chain_.is_direct() || !proxy_chain_.Last().is_quic()) {
    return ERR_NO_SUPPORTED_PROXIES;
  }

  auto [proxy_chain_prefix, proxy_server] = proxy_chain_.SplitLast();
  for (const ProxyServer& preceding_proxy :
       proxy_chain_prefix.proxy_servers()) {
    if (!preceding_proxy.is_quic()) {
      return ERR_NO_SUPPORTED_PROXIES;
    }
  }

  next_state_ = State::kCreateSessionComplete;

  const HostPortPair& proxy_endpoint = proxy_server.host_port_pair();
  url::SchemeHostPort destination(url::kHttpsScheme,
                                  proxy_endpoint.HostForURL(),
                                  proxy_endpoint.port());

  session_request_ =
      std::make_unique<QuicSessionRequest>(session_->quic_session_pool());
  return session_request_->Request(
      destination, SupportedQuicVersionForProxying(), proxy_chain_prefix,
      traffic_annotation_, session_->context().http_user_agent_settings,
      SessionUsage::kProxy, PRIVACY_MODE_DISABLED, DEFAULT_PRIORITY, SocketTag(),
      network_anonymization_key_, SecureDnsPolicy::kDisable,
      /*require_dns_https_alpn=*/false,
      CertVerifier::VERIFY_DISABLE_NETWORK_FETCHES,
      GURL("https://" + proxy_endpoint.ToString()),
      handles::kInvalidNetworkHandle, net_log_, &net_error_details_,
      MultiplexedSessionCreationInitiator::kUnknown,
      /*management_config=*/std::nullopt,
      /*failed_on_default_network_callback=*/CompletionOnceCallback(),
      base::BindOnce(&NaiveQuicProxyStreamRequest::OnIOComplete,
                     weak_ptr_factory_.GetWeakPtr()));
}

int NaiveQuicProxyStreamRequest::DoCreateSessionComplete(int result) {
  if (result < 0) {
    session_request_.reset();
    return result;
  }

  session_handle_ = session_request_->ReleaseSessionHandle();
  session_request_.reset();
  CHECK(session_handle_);

  next_state_ = State::kCreateStreamComplete;
  return session_handle_->RequestStream(
      /*requires_confirmation=*/false,
      base::BindOnce(&NaiveQuicProxyStreamRequest::OnIOComplete,
                     weak_ptr_factory_.GetWeakPtr()),
      traffic_annotation_);
}

int NaiveQuicProxyStreamRequest::DoCreateStreamComplete(int result) {
  if (result < 0) {
    return result;
  }

  stream_ = session_handle_->ReleaseStream();
  if (!stream_ || !stream_->IsOpen()) {
    stream_.reset();
    return ERR_CONNECTION_CLOSED;
  }

  uint8_t urgency = ConvertRequestPriorityToQuicPriority(DEFAULT_PRIORITY);
  stream_->SetPriority(quic::QuicStreamPriority(
      quic::HttpStreamPriority{urgency, kDefaultPriorityIncremental}));
  return OK;
}

}  // namespace net
