// Copyright 2018 The Chromium Authors. All rights reserved.
// Copyright 2018 klzgrad <kizdiv@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/tools/naive/naive_proxy.h"

#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "base/location.h"
#include "base/logging.h"
#include "base/task/single_thread_task_runner.h"
#include "base/time/time.h"
#include "build/build_config.h"
#include "net/base/load_flags.h"
#include "net/base/net_errors.h"
#include "net/http/http_network_session.h"
#include "net/proxy_resolution/configured_proxy_resolution_service.h"
#include "net/proxy_resolution/proxy_config.h"
#include "net/proxy_resolution/proxy_list.h"
#include "net/socket/client_socket_pool_manager.h"
#include "net/socket/server_socket.h"
#include "net/socket/stream_socket.h"
#include "net/socket/udp_server_socket.h"
#include "net/tools/naive/http_proxy_server_socket.h"
#include "net/tools/naive/naive_proxy_delegate.h"
#include "net/tools/naive/socks5_server_socket.h"
#include "net/tools/naive/socks5_udp_association.h"

namespace net {
namespace {
constexpr base::TimeDelta kIdleCheckPeriod = base::Minutes(1);

IPAddress NormalizeAddress(const IPAddress& address) {
  return address.IsIPv4MappedIPv6() ? ConvertIPv4MappedIPv6ToIPv4(address)
                                    : address;
}
}  // namespace

struct NaiveProxy::PendingSocksHandshake {
  enum class Action {
    kClose,
    kStartTcp,
    kStartUdp,
  };

  PendingSocksHandshake(
      std::unique_ptr<PaddingType> negotiated_client_padding,
      const NetworkAnonymizationKey& network_anonymization_key,
      std::unique_ptr<Socks5ServerSocket> socket)
      : negotiated_client_padding(std::move(negotiated_client_padding)),
        network_anonymization_key(network_anonymization_key),
        socket(std::move(socket)) {}
  ~PendingSocksHandshake() = default;

  std::unique_ptr<PaddingType> negotiated_client_padding;
  NetworkAnonymizationKey network_anonymization_key;
  std::unique_ptr<Socks5ServerSocket> socket;
  std::unique_ptr<DatagramServerSocket> relay_socket;
  std::unique_ptr<Socks5UdpDatagramBackend> backend;
  IPEndPoint control_peer;
  IPEndPoint relay_endpoint;
  Action action = Action::kClose;
  base::TimeTicks created_at = base::TimeTicks::Now();
};

NaiveProxy::Tunnel::Tunnel() = default;
NaiveProxy::Tunnel::~Tunnel() = default;

NaiveProxy::NaiveProxy(std::unique_ptr<ServerSocket> listen_socket,
                       ClientProtocol protocol,
                       const std::string& listen_user,
                       const std::string& listen_pass,
                       int concurrency,
                       int tunnel_timeout,
                       int idle_timeout,
                       RedirectResolver* resolver,
                       HttpNetworkSession* session,
                       const NetworkTrafficAnnotationTag& traffic_annotation,
                       const std::vector<PaddingType>& supported_padding_types,
                       Socks5UdpBackendFactory udp_backend_factory)
    : listen_socket_(std::move(listen_socket)),
      protocol_(protocol),
      listen_user_(listen_user),
      listen_pass_(listen_pass),
      concurrency_(concurrency),
      tunnel_timeout_(base::Seconds(tunnel_timeout)),
      idle_timeout_(base::Seconds(idle_timeout)),
      resolver_(resolver),
      session_(session),
      net_log_(
          NetLogWithSource::Make(session->net_log(), NetLogSourceType::NONE)),
      next_id_(0),
      next_state_(State::kAccept),
      tunnels_(concurrency),
      udp_backend_factory_(std::move(udp_backend_factory)),
      traffic_annotation_(traffic_annotation),
      supported_padding_types_(supported_padding_types) {
  const auto& proxy_config = static_cast<ConfiguredProxyResolutionService*>(
                                 session_->proxy_resolution_service())
                                 ->config();
  DCHECK(proxy_config);
  const ProxyList& proxy_list =
      proxy_config.value().value().proxy_rules().single_proxies;
  DCHECK(!proxy_list.IsEmpty());
  proxy_info_.UseProxyList(proxy_list);
  proxy_info_.set_traffic_annotation(
      net::MutableNetworkTrafficAnnotationTag(traffic_annotation_));
  if (!proxy_info_.is_direct()) {
    const ProxyChain& proxy_chain = proxy_info_.proxy_chain();
    std::tie(last_proxy_partial_chain_, last_proxy_server_) =
        proxy_chain.SplitLast();
  }

  DCHECK(listen_socket_);
  // Start accepting connections in next run loop in case when delegate is not
  // ready to get callbacks.
  io_callback_ = base::BindRepeating(&NaiveProxy::OnIOComplete,
                                     weak_ptr_factory_.GetWeakPtr());
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&NaiveProxy::OnIOComplete,
                                weak_ptr_factory_.GetWeakPtr(), OK));

  cleanup_timer_.Start(FROM_HERE, kIdleCheckPeriod, this,
                       &NaiveProxy::CleanUpIdleConnections);
}

NaiveProxy::~NaiveProxy() = default;

void NaiveProxy::OnIOComplete(int result) {
  DCHECK_NE(next_state_, State::kNone);
  int rv = DoLoop(result);
  if (rv != ERR_IO_PENDING) {
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&NaiveProxy::OnIOComplete,
                                  weak_ptr_factory_.GetWeakPtr(), OK));
  }
}

int NaiveProxy::DoLoop(int last_io_result) {
  DCHECK_NE(next_state_, State::kNone);
  int rv = last_io_result;
  do {
    State state = next_state_;
    next_state_ = State::kNone;
    switch (state) {
      case State::kAccept:
        DCHECK_EQ(OK, rv);
        rv = DoAccept();
        break;
      case State::kAcceptComplete:
        rv = DoAcceptComplete(rv);
        break;
      case State::kPreamble:
        DCHECK_EQ(OK, rv);
        rv = DoPreamble();
        break;
      case State::kPreambleComplete:
        rv = DoPreambleComplete(rv);
        break;
      case State::kConnect:
        DCHECK_EQ(OK, rv);
        rv = DoConnect();
        break;
      default:
        rv = ERR_UNEXPECTED;
        break;
    }
  } while (rv != ERR_IO_PENDING && next_state_ != State::kNone);
  return rv;
}

int NaiveProxy::DoAccept() {
  next_state_ = State::kAcceptComplete;
  return listen_socket_->Accept(&accepted_socket_, io_callback_);
}

int NaiveProxy::DoAcceptComplete(int result) {
  if (result != OK) {
    next_state_ = State::kAccept;
    LOG(ERROR) << "Accept error: " << ErrorToShortString(result);
    // This accept error is ignored to start the next accept.
    return OK;
  }

  Tunnel& tunnel = tunnels_[next_id_ % concurrency_];
  base::TimeTicks now = base::TimeTicks::Now();
  if (IsSessionCapable()) {
    if (tunnel.deadline.is_null()) {
      tunnel.deadline = now + tunnel_timeout_;
      next_state_ = State::kPreamble;
    } else if (now > tunnel.deadline) {
      tunnel.nak = NetworkAnonymizationKey::CreateTransient();
      tunnel.deadline = now + tunnel_timeout_;
      tunnel.url_getter.reset();
      next_state_ = State::kPreamble;
    } else {
      DCHECK(tunnel.url_getter != nullptr);
      tunnel.url_getter->StartOne();
      next_state_ = State::kConnect;
    }
  } else {
    next_state_ = State::kConnect;
  }
  return OK;
}

// Possible exit states: State::kAccept, State::kPreambleComplete
int NaiveProxy::DoPreamble() {
  Tunnel& tunnel = tunnels_[next_id_ % concurrency_];
  DCHECK(WillCreateSession(tunnel.nak));
  tunnel.url_getter = std::make_unique<PreambleGetter>(proxy_info_, session_,
                                                       tunnel.nak, net_log_);
  next_state_ = State::kPreambleComplete;
  return tunnel.url_getter->Start(io_callback_);
}

int NaiveProxy::DoPreambleComplete(int result) {
  if (result != OK) {
    LOG(WARNING) << "Preamble error: " << ErrorToShortString(result);
    // Preamble error doesn't prevent Connect().
  }
  next_state_ = State::kConnect;
  return OK;
}

int NaiveProxy::DoConnect() {
  auto negotiated_client_padding =
      std::make_unique<PaddingType>(PaddingType::kNone);

  // Once accepted_socket_ is moved, the next Accept can start.
  next_state_ = State::kAccept;

  const unsigned int connection_id = next_id_++;
  const Tunnel& tunnel = tunnels_[connection_id % concurrency_];

  std::unique_ptr<StreamSocket> socket;
  if (protocol_ == ClientProtocol::kSocks5) {
    auto socks_socket = std::make_unique<Socks5ServerSocket>(
        std::move(accepted_socket_), listen_user_, listen_pass_,
        traffic_annotation_);
    auto pending = std::make_unique<PendingSocksHandshake>(
        std::move(negotiated_client_padding), tunnel.nak,
        std::move(socks_socket));
    Socks5ServerSocket* pending_socket = pending->socket.get();
    pending_socks_by_id_[connection_id] = std::move(pending);
    int result = pending_socket->ReadRequest(base::BindOnce(
        &NaiveProxy::OnSocksRequestRead, weak_ptr_factory_.GetWeakPtr(),
        connection_id));
    if (result != ERR_IO_PENDING) {
      HandleSocksRequestRead(connection_id, result);
    }
    return OK;
  } else if (protocol_ == ClientProtocol::kHttp) {
    socket = std::make_unique<HttpProxyServerSocket>(
        std::move(accepted_socket_), listen_user_, listen_pass_,
        negotiated_client_padding.get(), traffic_annotation_,
        supported_padding_types_);
  } else if (protocol_ == ClientProtocol::kRedir) {
    socket = std::move(accepted_socket_);
  } else {
    return OK;
  }

  StartTcpConnection(connection_id, std::move(negotiated_client_padding),
                     tunnel.nak, std::move(socket));
  return OK;
}

void NaiveProxy::StartTcpConnection(
    unsigned int connection_id,
    std::unique_ptr<PaddingType> negotiated_client_padding,
    const NetworkAnonymizationKey& network_anonymization_key,
    std::unique_ptr<StreamSocket> socket) {
  auto connection_ptr = std::make_unique<NaiveConnection>(
      connection_id, protocol_, std::move(negotiated_client_padding),
      proxy_info_, resolver_, session_, network_anonymization_key, net_log_,
      std::move(socket), traffic_annotation_);
  auto* connection = connection_ptr.get();
  connection_by_id_[connection->id()] = std::move(connection_ptr);

  int result = connection->Connect(
      base::BindOnce(&NaiveProxy::OnConnectComplete,
                     weak_ptr_factory_.GetWeakPtr(), connection->id()));
  if (result == ERR_IO_PENDING) {
    return;
  }
  HandleConnectResult(connection, result);
}

void NaiveProxy::OnSocksRequestRead(unsigned int connection_id, int result) {
  HandleSocksRequestRead(connection_id, result);
}

void NaiveProxy::HandleSocksRequestRead(unsigned int connection_id,
                                        int result) {
  auto it = pending_socks_by_id_.find(connection_id);
  if (it == pending_socks_by_id_.end()) {
    return;
  }
  PendingSocksHandshake* pending = it->second.get();
  if (result != OK) {
    ClosePendingSocks(connection_id, result);
    return;
  }

  Socks5ServerSocket::Reply reply = Socks5ServerSocket::Reply::kGeneralFailure;
  IPEndPoint bound_endpoint;
  switch (pending->socket->command()) {
    case Socks5ServerSocket::Command::kConnect:
      pending->action = PendingSocksHandshake::Action::kStartTcp;
      reply = Socks5ServerSocket::Reply::kSuccess;
      break;
    case Socks5ServerSocket::Command::kBind:
    case Socks5ServerSocket::Command::kUnsupported:
      pending->action = PendingSocksHandshake::Action::kClose;
      reply = Socks5ServerSocket::Reply::kCommandNotSupported;
      break;
    case Socks5ServerSocket::Command::kUdpAssociate:
      pending->action = PendingSocksHandshake::Action::kClose;
      if (CanUseNativeUdp() && HasNativeUdpAssociationCapacity() &&
          udp_backend_factory_) {
        result = pending->socket->GetPeerAddress(&pending->control_peer);
        if (result == OK) {
          result = BindUdpRelay(pending);
        }
        if (result == OK) {
          pending->backend = udp_backend_factory_.Run(Socks5UdpBackendContext(
              connection_id, session_, proxy_info_.proxy_chain(),
              pending->network_anonymization_key, net_log_,
              traffic_annotation_,
              Socks5UdpBackendLimits::kDefaultConnectTimeout,
              idle_timeout_));
        }
        if (result == OK && pending->backend) {
          pending->action = PendingSocksHandshake::Action::kStartUdp;
          reply = Socks5ServerSocket::Reply::kSuccess;
          bound_endpoint = pending->relay_endpoint;
        }
      }
      break;
  }

  result = pending->socket->WriteReply(
      reply, bound_endpoint,
      base::BindOnce(&NaiveProxy::OnSocksReplyWritten,
                     weak_ptr_factory_.GetWeakPtr(), connection_id));
  if (result != ERR_IO_PENDING) {
    HandleSocksReplyWritten(connection_id, result);
  }
}

void NaiveProxy::OnSocksReplyWritten(unsigned int connection_id, int result) {
  HandleSocksReplyWritten(connection_id, result);
}

void NaiveProxy::HandleSocksReplyWritten(unsigned int connection_id,
                                         int result) {
  auto it = pending_socks_by_id_.find(connection_id);
  if (it == pending_socks_by_id_.end()) {
    return;
  }
  PendingSocksHandshake* pending = it->second.get();
  const PendingSocksHandshake::Action action = pending->action;
  if (action == PendingSocksHandshake::Action::kClose || result != OK) {
    ClosePendingSocks(connection_id, result);
    return;
  }

  std::unique_ptr<PendingSocksHandshake> owned = std::move(it->second);
  pending_socks_by_id_.erase(it);
  if (action == PendingSocksHandshake::Action::kStartTcp) {
    StartTcpConnection(connection_id,
                       std::move(owned->negotiated_client_padding),
                       owned->network_anonymization_key,
                       std::move(owned->socket));
    return;
  }

  CHECK_EQ(action, PendingSocksHandshake::Action::kStartUdp);
  auto association = std::make_unique<Socks5UdpAssociation>(
      connection_id, std::move(owned->socket),
      std::move(owned->relay_socket), std::move(owned->backend),
      owned->control_peer);
  Socks5UdpAssociation* association_ptr = association.get();
  udp_association_by_id_[connection_id] = std::move(association);
  result = association_ptr->Start(base::BindOnce(
      &NaiveProxy::OnUdpAssociationComplete, weak_ptr_factory_.GetWeakPtr(),
      connection_id));
  if (result != ERR_IO_PENDING) {
    CloseUdpAssociation(connection_id, result);
  }
}

bool NaiveProxy::CanUseNativeUdp() const {
  if (proxy_info_.is_direct()) {
    return false;
  }
  const ProxyChain& chain = proxy_info_.proxy_chain();
  if (chain.is_direct() || !chain.Last().is_quic()) {
    return false;
  }
  for (const ProxyServer& proxy : chain.proxy_servers()) {
    if (!proxy.is_quic()) {
      return false;
    }
  }
  return true;
}

bool NaiveProxy::HasNativeUdpAssociationCapacity() const {
  size_t count = udp_association_by_id_.size();
  for (const auto& [id, pending] : pending_socks_by_id_) {
    if (pending->action == PendingSocksHandshake::Action::kStartUdp) {
      ++count;
    }
  }
  return count < Socks5UdpBackendLimits::kMaxActiveAssociationsPerProxy;
}

int NaiveProxy::BindUdpRelay(PendingSocksHandshake* pending) {
  IPEndPoint local_endpoint;
  int result = pending->socket->GetLocalAddress(&local_endpoint);
  if (result != OK) {
    return result;
  }
  const IPAddress bind_address = NormalizeAddress(local_endpoint.address());
  auto relay =
      std::make_unique<UDPServerSocket>(session_->net_log(), NetLogSource());
  result = relay->Listen(IPEndPoint(bind_address, 0));
  if (result != OK) {
    return result;
  }
  result = relay->GetLocalAddress(&pending->relay_endpoint);
  if (result != OK) {
    return result;
  }
  pending->relay_socket = std::move(relay);
  return OK;
}

void NaiveProxy::ClosePendingSocks(unsigned int connection_id, int reason) {
  auto it = pending_socks_by_id_.find(connection_id);
  if (it == pending_socks_by_id_.end()) {
    return;
  }
  VLOG(1) << "SOCKS connection " << connection_id
          << " closed during handshake: " << ErrorToShortString(reason);
  auto pending = std::move(it->second);
  pending_socks_by_id_.erase(it);
  base::SingleThreadTaskRunner::GetCurrentDefault()->DeleteSoon(
      FROM_HERE, std::move(pending));
}

void NaiveProxy::OnUdpAssociationComplete(unsigned int connection_id,
                                          int result) {
  CloseUdpAssociation(connection_id, result);
}

void NaiveProxy::CloseUdpAssociation(unsigned int connection_id, int reason) {
  auto it = udp_association_by_id_.find(connection_id);
  if (it == udp_association_by_id_.end()) {
    return;
  }
  VLOG(1) << "SOCKS5 UDP association " << connection_id
          << " closed: " << ErrorToShortString(reason);
  auto association = std::move(it->second);
  udp_association_by_id_.erase(it);
  base::SingleThreadTaskRunner::GetCurrentDefault()->DeleteSoon(
      FROM_HERE, std::move(association));
}

void NaiveProxy::OnConnectComplete(unsigned int connection_id, int result) {
  auto* connection = FindConnection(connection_id);
  if (!connection) {
    return;
  }
  HandleConnectResult(connection, result);
}

void NaiveProxy::HandleConnectResult(NaiveConnection* connection, int result) {
  if (result != OK) {
    Close(connection->id(), result);
    return;
  }
  DoRun(connection);
}

void NaiveProxy::DoRun(NaiveConnection* connection) {
  int result = connection->Run(base::BindOnce(&NaiveProxy::OnRunComplete,
                                              weak_ptr_factory_.GetWeakPtr(),
                                              connection->id()));
  if (result == ERR_IO_PENDING) {
    return;
  }
  HandleRunResult(connection, result);
}

void NaiveProxy::OnRunComplete(unsigned int connection_id, int result) {
  auto* connection = FindConnection(connection_id);
  if (!connection) {
    return;
  }
  HandleRunResult(connection, result);
}

void NaiveProxy::HandleRunResult(NaiveConnection* connection, int result) {
  Close(connection->id(), result);
}

void NaiveProxy::Close(unsigned int connection_id, int reason) {
  auto it = connection_by_id_.find(connection_id);
  if (it == connection_by_id_.end()) {
    return;
  }

  LOG(INFO) << "Connection " << connection_id
            << " closed: " << ErrorToShortString(reason);

  // The call stack might have callbacks which still have the pointer of
  // connection. Instead of referencing connection with ID all the time,
  // destroys the connection in next run loop to make sure any pending
  // callbacks in the call stack return.
  base::SingleThreadTaskRunner::GetCurrentDefault()->DeleteSoon(
      FROM_HERE, std::move(it->second));
  connection_by_id_.erase(it);
}

NaiveConnection* NaiveProxy::FindConnection(unsigned int connection_id) {
  auto it = connection_by_id_.find(connection_id);
  if (it == connection_by_id_.end()) {
    return nullptr;
  }
  return it->second.get();
}

NaiveProxyDelegate* NaiveProxy::naive_proxy_delegate() const {
  auto* proxy_delegate =
      static_cast<NaiveProxyDelegate*>(session_->context().proxy_delegate);
  DCHECK(proxy_delegate);
  return proxy_delegate;
}

bool NaiveProxy::IsSessionCapable() const {
  if (proxy_info_.is_direct()) {
    return false;
  }
  // TODO(klzgrad): HTTP/1 https proxy will fail
  return last_proxy_server_.is_secure_http_like();
}

bool NaiveProxy::WillCreateSession(const NetworkAnonymizationKey& nak) const {
  if (last_proxy_server_.is_https()) {
    SpdySessionKey key(last_proxy_server_.host_port_pair(),
                       PRIVACY_MODE_DISABLED, last_proxy_partial_chain_,
                       SessionUsage::kProxy, SocketTag(), nak,
                       SecureDnsPolicy::kDisable,
                       /*disable_cert_verification_network_fetches=*/true,
                       handles::kInvalidNetworkHandle);
    return !session_->spdy_session_pool()->FindAvailableSession(
        key, /*enable_ip_based_pooling_for_h2=*/false,
        /*is_websocket=*/false, net_log_);
  }
  if (last_proxy_server_.is_quic()) {
    QuicSessionKey key(
        last_proxy_server_.host_port_pair(), PRIVACY_MODE_DISABLED,
        last_proxy_partial_chain_, SessionUsage::kProxy, SocketTag(), nak,
        SecureDnsPolicy::kDisable, /*require_dns_https_alpn=*/false,
        /*disable_cert_verification_network_fetches=*/true,
        handles::kInvalidNetworkHandle);
    url::SchemeHostPort destination("https", last_proxy_server_.GetHost(),
                                    last_proxy_server_.GetPort(),
                                    url::SchemeHostPort::ALREADY_CANONICALIZED);
    return !session_->quic_session_pool()->CanUseExistingSession(key,
                                                                 destination);
  }
  return false;
}

void NaiveProxy::CleanUpIdleConnections() {
  std::vector<NaiveConnection*> idle_conns;
  std::vector<unsigned int> stale_pending_socks;
  std::vector<unsigned int> idle_udp_associations;
  base::TimeTicks now = base::TimeTicks::Now();
  for (const auto& [id, conn] : connection_by_id_) {
    base::TimeDelta idle = now - conn->GetLastWriteTime();
    base::TimeDelta age = now - conn->GetCreationTime();
    if (idle > idle_timeout_ || age > tunnel_timeout_) {
      idle_conns.push_back(conn.get());
    }
  }
  for (NaiveConnection* conn : idle_conns) {
    conn->Disconnect();
  }
  for (const auto& [id, pending] : pending_socks_by_id_) {
    if (now - pending->created_at > tunnel_timeout_) {
      stale_pending_socks.push_back(id);
    }
  }
  for (unsigned int id : stale_pending_socks) {
    ClosePendingSocks(id, ERR_TIMED_OUT);
  }
  for (const auto& [id, association] : udp_association_by_id_) {
    if (now - association->last_activity() > idle_timeout_) {
      idle_udp_associations.push_back(id);
    }
  }
  for (unsigned int id : idle_udp_associations) {
    CloseUdpAssociation(id, ERR_TIMED_OUT);
  }
  session_->CloseIdleConnections("Rotate old tunnels");
}
}  // namespace net
