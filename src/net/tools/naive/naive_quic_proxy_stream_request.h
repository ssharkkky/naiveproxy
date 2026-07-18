// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_TOOLS_NAIVE_NAIVE_QUIC_PROXY_STREAM_REQUEST_H_
#define NET_TOOLS_NAIVE_NAIVE_QUIC_PROXY_STREAM_REQUEST_H_

#include <memory>
#include <string>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "net/base/completion_once_callback.h"
#include "net/base/ip_endpoint.h"
#include "net/base/net_error_details.h"
#include "net/base/network_anonymization_key.h"
#include "net/base/proxy_chain.h"
#include "net/log/net_log_with_source.h"
#include "net/quic/quic_chromium_client_session.h"
#include "net/quic/quic_chromium_client_stream.h"
#include "net/traffic_annotation/network_traffic_annotation.h"

namespace net {

class HttpNetworkSession;
class QuicSessionRequest;

// Acquires an HTTP/3 request stream to the last QUIC proxy in a proxy chain.
//
// This is the Naive-owned boundary between a future CONNECT-UDP tunnel and
// Chromium's QUIC session pool. It deliberately does not send CONNECT-UDP or
// own datagram semantics. The caller must keep this object alive while using
// the released stream so the session handle and its address metadata remain
// available.
class NaiveQuicProxyStreamRequest {
 public:
  NaiveQuicProxyStreamRequest(
      HttpNetworkSession* session,
      const ProxyChain& proxy_chain,
      const NetworkAnonymizationKey& network_anonymization_key,
      const NetLogWithSource& net_log,
      const NetworkTrafficAnnotationTag& traffic_annotation);
  ~NaiveQuicProxyStreamRequest();

  NaiveQuicProxyStreamRequest(const NaiveQuicProxyStreamRequest&) = delete;
  NaiveQuicProxyStreamRequest& operator=(
      const NaiveQuicProxyStreamRequest&) = delete;

  // Returns OK or a network error synchronously, or ERR_IO_PENDING and invokes
  // `callback` exactly once when session and stream acquisition completes.
  int Start(CompletionOnceCallback callback);

  // May only be called once after Start() completes successfully.
  std::unique_ptr<QuicChromiumClientStream::Handle> ReleaseStream();

  int GetLocalAddress(IPEndPoint* address) const;
  int GetPeerAddress(IPEndPoint* address) const;
  std::string GetUserAgent() const;

  const NetErrorDetails& net_error_details() const {
    return net_error_details_;
  }

  const ProxyChain& proxy_chain() const { return proxy_chain_; }

 private:
  enum class State {
    kNone,
    kCreateSession,
    kCreateSessionComplete,
    kCreateStreamComplete,
  };

  void OnIOComplete(int result);
  int DoLoop(int last_io_result);
  int DoCreateSession();
  int DoCreateSessionComplete(int result);
  int DoCreateStreamComplete(int result);

  const raw_ptr<HttpNetworkSession> session_;
  const ProxyChain proxy_chain_;
  const NetworkAnonymizationKey network_anonymization_key_;
  const NetLogWithSource net_log_;
  const NetworkTrafficAnnotationTag traffic_annotation_;

  State next_state_ = State::kNone;
  CompletionOnceCallback callback_;
  NetErrorDetails net_error_details_;
  std::unique_ptr<QuicSessionRequest> session_request_;
  std::unique_ptr<QuicChromiumClientSession::Handle> session_handle_;
  std::unique_ptr<QuicChromiumClientStream::Handle> stream_;

  base::WeakPtrFactory<NaiveQuicProxyStreamRequest> weak_ptr_factory_{this};
};

}  // namespace net

#endif  // NET_TOOLS_NAIVE_NAIVE_QUIC_PROXY_STREAM_REQUEST_H_
