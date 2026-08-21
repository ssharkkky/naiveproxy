// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/quic_proxy_datagram_client_socket.h"

#include "base/compiler_specific.h"
#include "base/feature_list.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/strings/escape.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/stringprintf.h"
#include "base/types/expected.h"
#include "base/types/expected_macros.h"
#include "base/values.h"
#include "net/base/completion_once_callback.h"
#include "net/base/features.h"
#include "net/base/host_port_pair.h"
#include "net/base/net_errors.h"
#include "net/base/proxy_chain.h"
#include "net/base/proxy_delegate.h"
#include "net/base/proxy_server.h"
#include "net/http/http_auth_controller.h"
#include "net/http/http_log_util.h"
#include "net/http/http_request_headers.h"
#include "net/http/http_response_headers.h"
#include "net/log/net_log_source.h"
#include "net/log/net_log_source_type.h"
#include "net/spdy/spdy_http_utils.h"

namespace net {

// static
GURL QuicProxyDatagramClientSocket::BuildConnectUdpUrl(
    const ProxyServer& proxy_server,
    const HostPortPair& target) {
  const std::string escaped_target_host =
      base::EscapeQueryParamValue(target.host(), false);
  return GURL(base::StringPrintf(
      "https://%s/.well-known/masque/udp/%s/%d/",
      proxy_server.host_port_pair().ToString().c_str(),
      escaped_target_host.c_str(), target.port()));
}

QuicProxyDatagramClientSocket::QuicProxyDatagramClientSocket(
    const GURL& url,
    const ProxyChain& proxy_chain,
    const std::string& user_agent,
    const NetLogWithSource& source_net_log,
    scoped_refptr<HttpAuthController> auth_controller,
    ProxyDelegate* proxy_delegate,
    bool notify_proxy_delegate_of_response)
    : url_(url),
      proxy_chain_(proxy_chain),
      auth_(std::move(auth_controller)),
      proxy_delegate_(proxy_delegate),
      notify_proxy_delegate_of_response_(notify_proxy_delegate_of_response),
      user_agent_(user_agent),
      net_log_(NetLogWithSource::Make(
          source_net_log.net_log(),
          NetLogSourceType::QUIC_PROXY_DATAGRAM_CLIENT_SOCKET)) {
  CHECK_GE(proxy_chain.length(), 1u);
  request_.method = "CONNECT";
  request_.url = url_;

  net_log_.BeginEventReferencingSource(NetLogEventType::SOCKET_ALIVE,
                                       source_net_log.source());
}

QuicProxyDatagramClientSocket::~QuicProxyDatagramClientSocket() {
  Close();
  net_log_.EndEvent(NetLogEventType::SOCKET_ALIVE);
}

const HttpResponseInfo* QuicProxyDatagramClientSocket::GetConnectResponseInfo()
    const {
  return response_.headers.get() ? &response_ : nullptr;
}

bool QuicProxyDatagramClientSocket::IsConnectedForTesting() const {
  return IsConnectedForDatagramIO();
}

bool QuicProxyDatagramClientSocket::IsConnectedForDatagramIO() const {
  return next_state_ == STATE_CONNECT_COMPLETE && stream_handle_ &&
         stream_handle_->IsOpen();
}

size_t QuicProxyDatagramClientSocket::MaxPayloadSize() const {
  if (!IsConnectedForDatagramIO() ||
      !stream_handle_->SupportsH3Datagram()) {
    return 0;
  }
  const size_t max_http_datagram_payload =
      stream_handle_->GetMaxDatagramSize();
  // RFC 9298 prepends the one-byte Context ID zero to every UDP payload.
  return max_http_datagram_payload > 1 ? max_http_datagram_payload - 1 : 0;
}

int QuicProxyDatagramClientSocket::ConnectViaStream(
    const IPEndPoint& local_address,
    const IPEndPoint& proxy_peer_address,
    std::unique_ptr<QuicChromiumClientStream::Handle> stream,
    CompletionOnceCallback callback) {
  DCHECK(connect_callback_.is_null());

  local_address_ = local_address;
  proxy_peer_address_ = proxy_peer_address;
  stream_handle_ = std::move(stream);

  if (!stream_handle_->IsOpen()) {
    return ERR_CONNECTION_CLOSED;
  }

  stream_handle_->SetDatagramStreamCloseCallback(base::BindOnce(
      &QuicProxyDatagramClientSocket::OnStreamClosed,
      weak_factory_.GetWeakPtr()));

  // Register stream to receive HTTP/3 datagrams.
  stream_handle_->RegisterHttp3DatagramVisitor(this);
  datagram_visitor_registered_ = true;

  DCHECK_EQ(STATE_DISCONNECTED, next_state_);
  next_state_ = auth_ ? STATE_GENERATE_AUTH_TOKEN : STATE_CALCULATE_HEADERS;

  int rv = DoLoop(OK);
  if (rv == ERR_IO_PENDING) {
    connect_callback_ = std::move(callback);
  }
  return rv;
}

int QuicProxyDatagramClientSocket::Connect(const IPEndPoint& address) {
  NOTREACHED();
}

int QuicProxyDatagramClientSocket::ConnectAsync(
    const IPEndPoint& address,
    CompletionOnceCallback callback) {
  NOTREACHED();
}

int QuicProxyDatagramClientSocket::ConnectUsingDefaultNetworkAsync(
    const IPEndPoint& address,
    CompletionOnceCallback callback) {
  NOTREACHED();
}

int QuicProxyDatagramClientSocket::ConnectUsingNetwork(
    handles::NetworkHandle network,
    const IPEndPoint& address) {
  NOTREACHED();
}

int QuicProxyDatagramClientSocket::ConnectUsingDefaultNetwork(
    const IPEndPoint& address) {
  NOTREACHED();
}

int QuicProxyDatagramClientSocket::ConnectUsingNetworkAsync(
    handles::NetworkHandle network,
    const IPEndPoint& address,
    CompletionOnceCallback callback) {
  NOTREACHED();
}

void QuicProxyDatagramClientSocket::Close() {
  weak_factory_.InvalidateWeakPtrs();
  connect_callback_.Reset();
  read_callback_.Reset();
  read_buf_len_ = 0;
  read_buf_ = nullptr;
  last_read_was_datagram_ = false;

  next_state_ = STATE_DISCONNECTED;

  if (datagram_visitor_registered_) {
    stream_handle_->UnregisterHttp3DatagramVisitor();
    datagram_visitor_registered_ = false;
  }

  connect_request_sent_ = false;
  awaiting_connect_response_ = false;

  stream_handle_->Reset(quic::QUIC_STREAM_CANCELLED);
}

int QuicProxyDatagramClientSocket::SetReceiveBufferSize(int32_t size) {
  return OK;
}

int QuicProxyDatagramClientSocket::SetSendBufferSize(int32_t size) {
  return OK;
}

void QuicProxyDatagramClientSocket::OnHttp3Datagram(
    quic::QuicStreamId stream_id,
    std::string_view payload) {
  DCHECK_EQ(stream_id, stream_handle_->id())
      << "Received datagram for unexpected stream.";

  quic::QuicDataReader reader(payload);
  uint64_t context_id;
  if (!reader.ReadVarInt62(&context_id)) {
    DLOG(WARNING)
        << "Ignoring HTTP Datagram payload. Failed to read context ID";
    return;
  }
  if (context_id != 0) {
    DLOG(WARNING) << "Ignoring HTTP Datagram with unrecognized context ID "
                  << context_id;
    return;
  }
  std::string_view http_payload = reader.ReadRemainingPayload();

  // If there's a read callback, process the payload immediately.
  if (read_callback_) {
    int result;
    int bytes_read = http_payload.size();
    if (http_payload.size() > static_cast<std::size_t>(read_buf_len_)) {
      result = ERR_MSG_TOO_BIG;
    } else {
      CHECK(read_buf_ != nullptr);
      CHECK(read_buf_len_ > 0);

      read_buf_->span().copy_prefix_from(base::as_byte_span(http_payload));
      result = bytes_read;
    }

    read_buf_ = nullptr;
    read_buf_len_ = 0;
    last_read_was_datagram_ = true;
    std::move(read_callback_).Run(result);

  } else {
    base::UmaHistogramBoolean(kMaxQueueSizeHistogram,
                              datagrams_.size() >= kMaxDatagramQueueSize);
    if (datagrams_.size() >= kMaxDatagramQueueSize) {
      DLOG(WARNING) << "Dropping datagram because queue is full";
      return;
    }

    // If no read callback, store the payload in the queue.
    datagrams_.emplace(http_payload.data(), http_payload.size());
  }
}

// Silently ignore unknown capsules.
void QuicProxyDatagramClientSocket::OnUnknownCapsule(
    quic::QuicStreamId stream_id,
    const quiche::UnknownCapsule& capsule) {}

// Proxied connections are not on any specific network.
handles::NetworkHandle QuicProxyDatagramClientSocket::GetBoundNetwork() const {
  return handles::kInvalidNetworkHandle;
}

// TODO(crbug.com/41497362): Implement method.
void QuicProxyDatagramClientSocket::ApplySocketTag(const SocketTag& tag) {}

int QuicProxyDatagramClientSocket::SetMulticastInterface(
    uint32_t interface_index) {
  NOTREACHED();
}

void QuicProxyDatagramClientSocket::SetIOSNetworkServiceType(
    int ios_network_service_type) {}

int QuicProxyDatagramClientSocket::GetPeerAddress(IPEndPoint* address) const {
  *address = proxy_peer_address_;
  return OK;
}

int QuicProxyDatagramClientSocket::GetLocalAddress(IPEndPoint* address) const {
  *address = local_address_;
  return OK;
}

void QuicProxyDatagramClientSocket::UseNonBlockingIO() {
  NOTREACHED();
}

int QuicProxyDatagramClientSocket::SetDoNotFragment() {
  NOTREACHED();
}

int QuicProxyDatagramClientSocket::SetRecvTos() {
  NOTREACHED();
}

int QuicProxyDatagramClientSocket::SetTos(net::DiffServCodePoint dscp,
                                          net::EcnCodePoint ecn) {
  return OK;
}

void QuicProxyDatagramClientSocket::SetMsgConfirm(bool confirm) {
  NOTREACHED();
}

const NetLogWithSource& QuicProxyDatagramClientSocket::NetLog() const {
  return net_log_;
}

net::DscpAndEcn QuicProxyDatagramClientSocket::GetLastTos() const {
  return {net::DSCP_DEFAULT, net::ECN_DEFAULT};
}

int QuicProxyDatagramClientSocket::Read(IOBuffer* buf,
                                        int buf_len,
                                        CompletionOnceCallback callback) {
  CHECK(connect_callback_.is_null());
  CHECK(read_callback_.is_null());
  CHECK(!read_buf_);
  CHECK(read_buf_len_ == 0);
  last_read_was_datagram_ = false;

  if (next_state_ == STATE_DISCONNECTED) {
    return ERR_SOCKET_NOT_CONNECTED;
  }

  // Return 0 if stream closed, signaling end-of-file or no more data.
  if (!stream_handle_->IsOpen()) {
    return 0;
  }

  // If there are datagrams available, attempt to read the first one into the
  // buffer.
  if (!datagrams_.empty()) {
    auto& datagram = datagrams_.front();
    int result;
    int bytes_read = datagram.size();

    if (datagram.size() > static_cast<std::size_t>(buf_len)) {
      result = ERR_MSG_TOO_BIG;
    } else {
      buf->span().copy_prefix_from(base::as_byte_span(datagram));
      result = bytes_read;
    }
    datagrams_.pop();
    last_read_was_datagram_ = true;
    return result;
  }

  // Save read callback so we can call it next time we receive a datagram.
  read_callback_ = std::move(callback);
  read_buf_ = buf;
  read_buf_len_ = buf_len;
  return ERR_IO_PENDING;
}

int QuicProxyDatagramClientSocket::Write(
    IOBuffer* buf,
    int buf_len,
    CompletionOnceCallback callback,
    const NetworkTrafficAnnotationTag& traffic_annotation) {
  DCHECK(connect_callback_.is_null());

  if (!connect_request_sent_) {
    return ERR_SOCKET_NOT_CONNECTED;
  }

  // UDP destinations and payload bytes are deliberately excluded from NetLog
  // at every capture mode. Only the byte count is operationally useful here.
  net_log_.AddEventWithIntParams(NetLogEventType::SOCKET_BYTES_SENT,
                                 "byte_count", buf_len);

  std::string_view packet(buf->data(), buf_len);
  int rv = stream_handle_->WriteConnectUdpPayload(packet);
  if (rv == OK) {
    return buf_len;
  }
  return rv;
}

void QuicProxyDatagramClientSocket::OnIOComplete(int result) {
  DCHECK_NE(STATE_DISCONNECTED, next_state_);

  // If the client didn't wait for a connect response so that it could
  // immediately start writing, get it ready to resume the full process of
  // tunnel establishment.
  if (awaiting_connect_response_) {
    next_state_ = STATE_READ_REPLY;
    awaiting_connect_response_ = false;
  }

  int rv = DoLoop(result);
  if (rv != ERR_IO_PENDING) {
    // Connect() finished (successfully or unsuccessfully) but we may not have
    // a connect_callback if we didn't wait for the response before considering
    // the connection established.
    if (!connect_callback_.is_null()) {
      std::move(connect_callback_).Run(rv);
    }
  }
}

void QuicProxyDatagramClientSocket::OnStreamClosed(int result) {
  // Cancel auth/header continuations before publishing the terminal state.
  // They may otherwise re-enter OnIOComplete after this close notification.
  weak_factory_.InvalidateWeakPtrs();
  next_state_ = STATE_DISCONNECTED;
  // Match Close(): do not clear the flag while leaving the visitor registered.
  if (datagram_visitor_registered_) {
    stream_handle_->UnregisterHttp3DatagramVisitor();
    datagram_visitor_registered_ = false;
  }
  connect_request_sent_ = false;
  awaiting_connect_response_ = false;
  read_buf_ = nullptr;
  read_buf_len_ = 0;
  last_read_was_datagram_ = false;

  CompletionOnceCallback callback;
  if (read_callback_) {
    callback = std::move(read_callback_);
  } else if (connect_callback_) {
    callback = std::move(connect_callback_);
  }
  if (callback) {
    std::move(callback).Run(result < 0 ? result : ERR_CONNECTION_CLOSED);
  }
}

int QuicProxyDatagramClientSocket::DoLoop(int last_io_result) {
  DCHECK_NE(next_state_, STATE_DISCONNECTED);
  int rv = last_io_result;
  do {
    State state = next_state_;
    next_state_ = STATE_DISCONNECTED;
    switch (state) {
      case STATE_GENERATE_AUTH_TOKEN:
        DCHECK_EQ(OK, rv);
        rv = DoGenerateAuthToken();
        break;
      case STATE_GENERATE_AUTH_TOKEN_COMPLETE:
        rv = DoGenerateAuthTokenComplete(rv);
        break;
      case STATE_CALCULATE_HEADERS:
        DCHECK_EQ(OK, rv);
        rv = DoCalculateHeaders();
        break;
      case STATE_CALCULATE_HEADERS_COMPLETE:
        rv = DoCalculateHeadersComplete(rv);
        break;
      case STATE_SEND_REQUEST:
        DCHECK_EQ(OK, rv);
        net_log_.BeginEvent(
            NetLogEventType::HTTP_TRANSACTION_TUNNEL_SEND_REQUEST);
        rv = DoSendRequest();
        break;
      case STATE_SEND_REQUEST_COMPLETE:
        net_log_.EndEventWithNetErrorCode(
            NetLogEventType::HTTP_TRANSACTION_TUNNEL_SEND_REQUEST, rv);
        rv = DoSendRequestComplete(rv);
        break;
      case STATE_READ_REPLY:
        rv = DoReadReply();
        break;
      case STATE_READ_REPLY_COMPLETE:
        rv = DoReadReplyComplete(rv);
        net_log_.EndEventWithNetErrorCode(
            NetLogEventType::HTTP_TRANSACTION_TUNNEL_READ_HEADERS, rv);
        break;
      case STATE_PROCESS_RESPONSE_HEADERS:
        DCHECK_EQ(OK, rv);
        rv = DoProcessResponseHeaders();
        break;
      case STATE_PROCESS_RESPONSE_HEADERS_COMPLETE:
        rv = DoProcessResponseHeadersComplete(rv);
        break;
      case STATE_PROCESS_RESPONSE_CODE:
        DCHECK_EQ(OK, rv);
        rv = DoProcessResponseCode();
        break;
      default:
        NOTREACHED() << "bad state";
    }
  } while (rv != ERR_IO_PENDING && next_state_ != STATE_DISCONNECTED &&
           next_state_ != STATE_CONNECT_COMPLETE);
  return rv;
}

int QuicProxyDatagramClientSocket::DoGenerateAuthToken() {
  CHECK(auth_);
  next_state_ = STATE_GENERATE_AUTH_TOKEN_COMPLETE;
  return auth_->MaybeGenerateAuthToken(
      &request_,
      base::BindOnce(&QuicProxyDatagramClientSocket::OnIOComplete,
                     weak_factory_.GetWeakPtr()),
      net_log_);
}

int QuicProxyDatagramClientSocket::DoGenerateAuthTokenComplete(int result) {
  DCHECK_NE(ERR_IO_PENDING, result);
  if (result == OK) {
    next_state_ = STATE_CALCULATE_HEADERS;
  }
  return result;
}

int QuicProxyDatagramClientSocket::DoCalculateHeaders() {
  next_state_ = STATE_CALCULATE_HEADERS_COMPLETE;

  authorization_headers_.Clear();
  proxy_delegate_headers_.Clear();

  if (auth_ && auth_->HaveAuth()) {
    auth_->AddAuthorizationHeader(&authorization_headers_);
  }

  if (proxy_delegate_) {
    ASSIGN_OR_RETURN(
        proxy_delegate_headers_,
        proxy_delegate_->OnBeforeTunnelRequest(
            proxy_chain_, proxy_chain_index(),
            base::BindOnce(
                &QuicProxyDatagramClientSocket::OnBeforeTunnelRequestComplete,
                weak_factory_.GetWeakPtr())),
        [](const auto& e) {
          // Success should always be reported via a base::expected containing
          // an HttpRequestHeaders, see ProxyDelegate::OnBeforeTunnelRequest.
          CHECK_NE(OK, e);
          return e;
        });
  }
  return OK;
}

int QuicProxyDatagramClientSocket::DoCalculateHeadersComplete(int result) {
  DCHECK_NE(ERR_IO_PENDING, result);
  if (result != OK) {
    return result;
  }
  next_state_ = STATE_SEND_REQUEST;

  request_.extra_headers.MergeFrom(authorization_headers_);
  request_.extra_headers.MergeFrom(proxy_delegate_headers_);

  return result;
}

int QuicProxyDatagramClientSocket::DoSendRequest() {
  next_state_ = STATE_SEND_REQUEST_COMPLETE;

  if (!url_.has_host()) {
    return ERR_ADDRESS_INVALID;
  }
  std::string host = url_.GetHost();
  int port = url_.IntPort();
  std::string host_and_port =
      url_.has_port() ? base::StrCat({host, ":", base::NumberToString(port)})
                      : std::move(host);
  request_.extra_headers.SetHeader(HttpRequestHeaders::kHost, host_and_port);



  if (!user_agent_.empty()) {
    request_.extra_headers.SetHeader(HttpRequestHeaders::kUserAgent,
                                     user_agent_);
  }

  request_.extra_headers.SetHeader("capsule-protocol", "?1");

  // Generate a deliberately redacted request line for logging purposes. The
  // real :path still goes into the H3 header block below, but the CONNECT-UDP
  // target must never appear in NetLog.
  constexpr char kRedactedRequestLine[] =
      "CONNECT-UDP [redacted] HTTP/3\r\n";
  NetLogRequestHeaders(net_log_,
                       NetLogEventType::HTTP_TRANSACTION_SEND_TUNNEL_HEADERS,
                       kRedactedRequestLine, &request_.extra_headers);

  quiche::HttpHeaderBlock headers;
  CreateSpdyHeadersFromHttpRequestForExtendedConnect(
      request_, /*priority=*/std::nullopt, "connect-udp",
      request_.extra_headers, &headers);

  int result = stream_handle_->WriteHeaders(std::move(headers), /*fin=*/false,
                                            /*ack_notifier_delegate=*/nullptr);
  connect_request_sent_ = true;
  return result;
}

int QuicProxyDatagramClientSocket::DoSendRequestComplete(int result) {
  if (result >= 0) {
    // Wait for HEADERS frame from the server
    next_state_ = STATE_READ_REPLY;  // STATE_READ_REPLY_COMPLETE;
    result = OK;
  }

  if (result >= 0 || result == ERR_IO_PENDING) {
    // Emit extra event so can use the same events as HttpProxyClientSocket.
    net_log_.BeginEvent(NetLogEventType::HTTP_TRANSACTION_TUNNEL_READ_HEADERS);
  }

  return result;
}

int QuicProxyDatagramClientSocket::DoReadReply() {
  next_state_ = STATE_READ_REPLY_COMPLETE;

  int rv = stream_handle_->ReadInitialHeaders(
      &response_header_block_,
      base::BindOnce(
          &QuicProxyDatagramClientSocket::OnReadResponseHeadersComplete,
          weak_factory_.GetWeakPtr()));

  if (rv == ERR_IO_PENDING) {
    // If the feature is enabled, the stream supports H3 datagrams and we
    // haven't received a response to the CONNECT-UDP request yet, bypass
    // processing response headers and consider tunnel "established" so
    // datagrams can be sent and traffic is not blocked.
    if (base::FeatureList::IsEnabled(
            net::features::kUseQuicProxiesWithoutWaitingForConnectResponse) &&
        stream_handle_->SupportsH3Datagram()) {
      next_state_ = STATE_CONNECT_COMPLETE;
      awaiting_connect_response_ = true;
      return OK;
    }

    return ERR_IO_PENDING;
  }
  if (rv < 0) {
    return rv;
  }

  return ProcessResponseHeaders(response_header_block_);
}

int QuicProxyDatagramClientSocket::DoReadReplyComplete(int result) {
  if (result < 0) {
    return result;
  }

  next_state_ = STATE_PROCESS_RESPONSE_HEADERS;

  NetLogResponseHeaders(
      net_log_, NetLogEventType::HTTP_TRANSACTION_READ_TUNNEL_RESPONSE_HEADERS,
      response_.headers.get());

  return OK;
}

int QuicProxyDatagramClientSocket::DoProcessResponseHeaders() {
  next_state_ = STATE_PROCESS_RESPONSE_HEADERS_COMPLETE;

  // TODO(crbug.com/326437102): Add case for Proxy Authentication.
  if (proxy_delegate_ && notify_proxy_delegate_of_response_) {
    return proxy_delegate_->OnTunnelHeadersReceived(
        proxy_chain(), proxy_chain_index(), *response_.headers,
        base::BindOnce(&QuicProxyDatagramClientSocket::OnIOComplete,
                       weak_factory_.GetWeakPtr()));
  }

  return OK;
}

int QuicProxyDatagramClientSocket::DoProcessResponseHeadersComplete(
    int result) {
  DCHECK_NE(ERR_IO_PENDING, result);
  if (result != OK) {
    return result;
  }

  next_state_ = STATE_PROCESS_RESPONSE_CODE;
  return OK;
}

int QuicProxyDatagramClientSocket::DoProcessResponseCode() {
  switch (response_.headers->response_code()) {
    case 200:  // OK
      next_state_ = STATE_CONNECT_COMPLETE;
      return OK;

    default:
      // Ignore response to avoid letting the proxy impersonate the target
      // server.  (See http://crbug.com/137891.)
      return ERR_TUNNEL_CONNECTION_FAILED;
  }
}

void QuicProxyDatagramClientSocket::OnReadResponseHeadersComplete(int result) {
  // Convert the now-populated quiche::HttpHeaderBlock to HttpResponseInfo
  if (result > 0) {
    result = ProcessResponseHeaders(response_header_block_);
  }

  if (result != ERR_IO_PENDING) {
    OnIOComplete(result);
  }
}

int QuicProxyDatagramClientSocket::ProcessResponseHeaders(
    const quiche::HttpHeaderBlock& headers) {
  if (SpdyHeadersToHttpResponse(headers, &response_) != OK) {
    DLOG(WARNING) << "Invalid headers";
    return ERR_QUIC_PROTOCOL_ERROR;
  }
  return OK;
}

void QuicProxyDatagramClientSocket::OnBeforeTunnelRequestComplete(
    base::expected<HttpRequestHeaders, Error> result) {
  if (result.has_value()) {
    proxy_delegate_headers_ = std::move(result.value());
    OnIOComplete(OK);
  } else {
    // OnBeforeTunnelRequestComplete should never report ERR_IO_PENDING since
    // it's used to signal that IO has completed.
    CHECK_NE(ERR_IO_PENDING, result.error());
    // Success should always be reported via a base::expected containing an
    // HttpRequestHeaders, see ProxyDelegate::OnBeforeTunnelRequest.
    CHECK_NE(OK, result.error());
    OnIOComplete(result.error());
  }
}

}  // namespace net
